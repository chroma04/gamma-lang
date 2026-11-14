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
typedef struct gm_cs2lqoac0a5ng4 { _Bool f0; uint32_t f1; uint64_t f2; } gm_cs2lqoac0a5ng4;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { double f0; } gm_tidu2er0d7h95;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_cs2lqoac0a5ng4) == 16 && _Alignof(gm_cs2lqoac0a5ng4) == 8 && offsetof(gm_cs2lqoac0a5ng4, f0) == 0 && offsetof(gm_cs2lqoac0a5ng4, f1) == 4 && offsetof(gm_cs2lqoac0a5ng4, f2) == 8, "layout of `IntInfo`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; uint64_t f4; } gm_t0j6181meiu4s;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; uint32_t f3; uint32_t f4; } gm_t133qiba7tvhc;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_t5g5vgung6h6s;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; uint64_t f4; gm_b16_8 f5; uint64_t f6; gm_b16_8 f7; } gm_t6q08imqe2768;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t f3; uint8_t p4[3]; gm_b16_8 f4; gm_b16_8 f5; } gm_t8fvq98mjik04;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; } gm_tg1t0c5hkovn5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; } gm_thognv9odov03;
_Static_assert(sizeof(gm_tidu2er0d7h95) == 8 && _Alignof(gm_tidu2er0d7h95) == 8 && offsetof(gm_tidu2er0d7h95, f0) == 0, "layout of `(f64,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint32_t f3; uint8_t pe[4]; } gm_tj4tbafenpsjm;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; } gm_tk6rvhh54g86p;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; gm_b16_8 f4; } gm_tpm6m3o0hprmc;
typedef struct { gm_tuglhjjmj2g58 f0; uint8_t f1; uint8_t pe[7]; } gm_tqcc6dmlne11b;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; uint64_t f5; gm_b16_8 f6; uint64_t f7; } gm_tsroeipraj6j8;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; } gm_tttl9nsdo1iu6;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0j6181meiu4s) == 72 && _Alignof(gm_t0j6181meiu4s) == 8 && offsetof(gm_t0j6181meiu4s, f0) == 0 && offsetof(gm_t0j6181meiu4s, f1) == 16 && offsetof(gm_t0j6181meiu4s, f2) == 32 && offsetof(gm_t0j6181meiu4s, f3) == 48 && offsetof(gm_t0j6181meiu4s, f4) == 64, "layout of `(string, string, string, string, u64)`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t133qiba7tvhc) == 48 && _Alignof(gm_t133qiba7tvhc) == 8 && offsetof(gm_t133qiba7tvhc, f0) == 0 && offsetof(gm_t133qiba7tvhc, f1) == 8 && offsetof(gm_t133qiba7tvhc, f2) == 24 && offsetof(gm_t133qiba7tvhc, f3) == 40 && offsetof(gm_t133qiba7tvhc, f4) == 44, "layout of `(u32, string, string, u32, u32)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t5g5vgung6h6s) == 32 && _Alignof(gm_t5g5vgung6h6s) == 8 && offsetof(gm_t5g5vgung6h6s, f0) == 0 && offsetof(gm_t5g5vgung6h6s, f1) == 8 && offsetof(gm_t5g5vgung6h6s, f2) == 24, "layout of `(u32, []u8, u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_th2lqbo77pvsc v1; } f1; } gm_t6j4d6ei14c2t;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t6q08imqe2768) == 104 && _Alignof(gm_t6q08imqe2768) == 8 && offsetof(gm_t6q08imqe2768, f0) == 0 && offsetof(gm_t6q08imqe2768, f1) == 16 && offsetof(gm_t6q08imqe2768, f2) == 32 && offsetof(gm_t6q08imqe2768, f3) == 40 && offsetof(gm_t6q08imqe2768, f4) == 56 && offsetof(gm_t6q08imqe2768, f5) == 64 && offsetof(gm_t6q08imqe2768, f6) == 80 && offsetof(gm_t6q08imqe2768, f7) == 88, "layout of `(string, string, usize, string, usize, string, usize, string)`");
_Static_assert(sizeof(gm_t8fvq98mjik04) == 72 && _Alignof(gm_t8fvq98mjik04) == 8 && offsetof(gm_t8fvq98mjik04, f0) == 0 && offsetof(gm_t8fvq98mjik04, f1) == 16 && offsetof(gm_t8fvq98mjik04, f2) == 32 && offsetof(gm_t8fvq98mjik04, f3) == 36 && offsetof(gm_t8fvq98mjik04, f4) == 40 && offsetof(gm_t8fvq98mjik04, f5) == 56, "layout of `([]u8, string, u32, bool, string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tg1t0c5hkovn5) == 128 && _Alignof(gm_tg1t0c5hkovn5) == 8 && offsetof(gm_tg1t0c5hkovn5, f0) == 0 && offsetof(gm_tg1t0c5hkovn5, f1) == 16 && offsetof(gm_tg1t0c5hkovn5, f2) == 32 && offsetof(gm_tg1t0c5hkovn5, f3) == 48 && offsetof(gm_tg1t0c5hkovn5, f4) == 64 && offsetof(gm_tg1t0c5hkovn5, f5) == 80 && offsetof(gm_tg1t0c5hkovn5, f6) == 96 && offsetof(gm_tg1t0c5hkovn5, f7) == 112, "layout of `([]u8, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thognv9odov03) == 40 && _Alignof(gm_thognv9odov03) == 8 && offsetof(gm_thognv9odov03, f0) == 0 && offsetof(gm_thognv9odov03, f1) == 8 && offsetof(gm_thognv9odov03, f2) == 24, "layout of `ForeignFn`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj4tbafenpsjm) == 56 && _Alignof(gm_tj4tbafenpsjm) == 8 && offsetof(gm_tj4tbafenpsjm, f0) == 0 && offsetof(gm_tj4tbafenpsjm, f1) == 16 && offsetof(gm_tj4tbafenpsjm, f2) == 32 && offsetof(gm_tj4tbafenpsjm, f3) == 48, "layout of `(string, string, string, u32)`");
_Static_assert(sizeof(gm_tk6rvhh54g86p) == 160 && _Alignof(gm_tk6rvhh54g86p) == 8 && offsetof(gm_tk6rvhh54g86p, f0) == 0 && offsetof(gm_tk6rvhh54g86p, f1) == 16 && offsetof(gm_tk6rvhh54g86p, f2) == 32 && offsetof(gm_tk6rvhh54g86p, f3) == 48 && offsetof(gm_tk6rvhh54g86p, f4) == 64 && offsetof(gm_tk6rvhh54g86p, f5) == 80 && offsetof(gm_tk6rvhh54g86p, f6) == 96 && offsetof(gm_tk6rvhh54g86p, f7) == 112 && offsetof(gm_tk6rvhh54g86p, f8) == 128 && offsetof(gm_tk6rvhh54g86p, f9) == 144, "layout of `(string, string, string, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpm6m3o0hprmc) == 64 && _Alignof(gm_tpm6m3o0hprmc) == 8 && offsetof(gm_tpm6m3o0hprmc, f0) == 0 && offsetof(gm_tpm6m3o0hprmc, f1) == 16 && offsetof(gm_tpm6m3o0hprmc, f2) == 24 && offsetof(gm_tpm6m3o0hprmc, f3) == 40 && offsetof(gm_tpm6m3o0hprmc, f4) == 48, "layout of `(string, u64, string, u64, string)`");
typedef struct { gm_tvdg94bk7aa22 f0; gm_tvdg94bk7aa22 f1; } gm_tq9t4giin3pm0;
_Static_assert(sizeof(gm_tqcc6dmlne11b) == 24 && _Alignof(gm_tqcc6dmlne11b) == 8 && offsetof(gm_tqcc6dmlne11b, f0) == 0 && offsetof(gm_tqcc6dmlne11b, f1) == 16, "layout of `?IntInfo`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsroeipraj6j8) == 112 && _Alignof(gm_tsroeipraj6j8) == 8 && offsetof(gm_tsroeipraj6j8, f0) == 0 && offsetof(gm_tsroeipraj6j8, f1) == 16 && offsetof(gm_tsroeipraj6j8, f2) == 32 && offsetof(gm_tsroeipraj6j8, f3) == 48 && offsetof(gm_tsroeipraj6j8, f4) == 64 && offsetof(gm_tsroeipraj6j8, f5) == 80 && offsetof(gm_tsroeipraj6j8, f6) == 88 && offsetof(gm_tsroeipraj6j8, f7) == 104, "layout of `(string, string, string, string, string, u64, string, u64)`");
_Static_assert(sizeof(gm_tttl9nsdo1iu6) == 56 && _Alignof(gm_tttl9nsdo1iu6) == 8 && offsetof(gm_tttl9nsdo1iu6, f0) == 0 && offsetof(gm_tttl9nsdo1iu6, f1) == 16 && offsetof(gm_tttl9nsdo1iu6, f2) == 32 && offsetof(gm_tttl9nsdo1iu6, f3) == 48, "layout of `(string, string, string, u64)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t6j4d6ei14c2t) == 40 && _Alignof(gm_t6j4d6ei14c2t) == 8 && offsetof(gm_t6j4d6ei14c2t, f0) == 0 && offsetof(gm_t6j4d6ei14c2t, f1) == 8, "layout of `Unsup!?string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tq9t4giin3pm0) == 24 && _Alignof(gm_tq9t4giin3pm0) == 4 && offsetof(gm_tq9t4giin3pm0, f0) == 0 && offsetof(gm_tq9t4giin3pm0, f1) == 12, "layout of `(Builtin, Builtin)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_alloc_tag_a71p35s {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* v */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_arith_asm_pklrmbh {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* outs */
    gm_trqfvmjtt4sij l13; /* ins */
    gm_trqfvmjtt4sij l20; /* clobbers */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_arith_get_jao7f8p {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_asm_text_fmvci6l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_assert_m8ussus {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_atomic_plj0t12 {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* ords */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_bit_count_77uni44 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_bits_layout_23rect9 {
    gm_frame h;
    gm_t57euh612ligr ret;
};
struct gm_fr_black_box_r6deojc {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_bool_ty_kotvo8t {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_builtin_s7ark4p {
    gm_frame h;
    gm_trqfvmjtt4sij l21; /* vals */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_convert_lqrk2n0 {
    gm_frame h;
    gm_tpautr4br7h3l l13; /* sd */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
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
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_from_tag_2m7nfln {
    gm_frame h;
    gm_tckdobunaulee l12; /* vd */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_ii_cty_i2b4e03 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ii_exact_g6a19dp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_ii_ucty_b40h5ot {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_unit_17uennd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_line_l6u87p9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_loc_u9c0aqb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_pow_s0lbtt4 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bytes_eocneng {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
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
struct gm_fr_rotate_tpbpt1c {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_slice_len_a3383br {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_slice_ptr_hojfgr5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_kk94re3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_sys_builtin_svdoho3 {
    gm_frame h;
    gm_trqfvmjtt4sij l12; /* cargs */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_8ekedsl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_ty_vgceilu {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
struct gm_fr_usize_ty_9mijhtc {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq0lfe1s2pin(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqg63o53ar52(const uint8_t *a, const uint8_t *b);
inline bool gm_eqov4e3mbi0f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_alloc_tag_a71p35s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_arith_asm_pklrmbh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_arith_fits_7degt3e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_arith_get_jao7f8p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3);
__attribute__((aligned(4))) double gm_f_arith_int_to_f64_tiud1hu(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_arith_pointee_align_vqoa1mk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_asm_text_fmvci6l(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_assert_m8ussus(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_atomic_plj0t12(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_bit_count_77uni44(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_black_box_r6deojc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_bound_n4p0vpb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_builtin_s7ark4p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_convert_lqrk2n0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) void gm_f_expr_porbc9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_fiber_builtin_bgfcp56(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_finish_int_cr0p55i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_tuglhjjmj2g58 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8, gm_b16_8 a9);
__attribute__((aligned(4))) gm_b16_8 gm_f_float_from_r6b8fu0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_00b54tv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0gjpujh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_15kpcel(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_21vnq4a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_25p33s1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_261kv0l(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_2g08abe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_2qdci62(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3itad0c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_3j59e2u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3s3fqnk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3vgp4uu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3vtl3te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4ok6i29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_53mv29j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5rrmec0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_68bbqsa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_6cn78ik(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6df13cd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7g5nhjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7gtji42(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7lru3c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_83msomh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_87dn9rn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_898275p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8d9jpvv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8m4h4ip(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8sn6h96(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_a1ch927(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_a1ec0ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_a7bth9i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_aculdom(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_aghnr00(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_b05vb48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bm6js5o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c57v5iu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c9b4vl3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_cjdn0sp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ckgfhm4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_da6unq6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dv5f37i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eqjcikj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eucvbnh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) void gm_f_format_fh9to9u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gf4f6rt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_gp2h96m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h95c7u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_he769rg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_iqjnutf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jagvq3q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_jj2aipm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jpvl2js(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kbs7h9q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ljsk7ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lqnh3lt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_m0j4lkh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_m7gv7a8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ms7m2d2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nkjq1if(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nm3fl8o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_o73frof(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ohk11b5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_p1ca3i8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_patlqq0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q296tqt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qofaffd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_s2ml2tu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tvr4di5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ucng4n7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_uhjjs86(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_uj77khu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v2sfrt0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fresh_tmp_6tadl69(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_from_tag_2m7nfln(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_fn_nlk82uj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_cty_i2b4e03(gm_cs2lqoac0a5ng4 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_ii_exact_g6a19dp(gm_cs2lqoac0a5ng4 a0);
__attribute__((aligned(4))) void gm_f_ii_max_gtp5h90(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0);
__attribute__((aligned(4))) void gm_f_ii_min_lku7pdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_ucty_b40h5ot(gm_cs2lqoac0a5ng4 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_cty_s62af0i(uint8_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_int_info_2tcdf1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_098d9va(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_0kkt9cm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_10398ks(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_156hint(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_21h063v(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_3utfuce(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_5q2h82g(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_6h7r936(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_6sel63v(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_6t3ttmd(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_7e19lva(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_7pb5lg6(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_81ikvle(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_a98fois(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ahjvru7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_biu9mas(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_bq6igl9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_calnga5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_di9ea86(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_evpuk0q(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_jl8o1sr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_jq6ksk1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_kafc4c5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ki2bckn(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_linqala(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_lm0b3ih(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_n685kk5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_o1esdvv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_oi4f7u8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qno93os(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_v355qja(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_vgjqckp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_vh7uebp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_load_0hebde7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_none_v2s8s0l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_slice_sa50efe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_some_9aqq928(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_variant_srnk8ha(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, const void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_jch381e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_place_addr_7ji15ot(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_pow_s0lbtt4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_rotate_tpbpt1c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_sys_builtin_svdoho3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_name_pcdoujk(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_vector_builtin_a1f1ati(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern const uint8_t gm_s02uuedsru1qu[9];
extern const uint8_t gm_s0cpkv7s4ef85[24];
extern const uint8_t gm_s0fg7tcl0p4ij[13];
extern const uint8_t gm_s0m9d22aen6f8[2];
extern const uint8_t gm_s12ng38ve3205[8];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s42h828lb45uf[19];
extern const uint8_t gm_s565fgm0tsngr[15];
extern const uint8_t gm_s63o5blu3j46d[16];
extern const uint8_t gm_s66fk044n1n0k[9];
extern const uint8_t gm_s9gk1b0ve64mt[10];
extern const uint8_t gm_sbb1ri5er9djq[4];
extern const uint8_t gm_sd2ht7fkpnftp[17];
extern const uint8_t gm_sdeu8nuc597e8[19];
extern const uint8_t gm_sg3ba4vpic0jb[19];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgjc0ebf56hf4[8];
extern const uint8_t gm_shhrbleiv8o3j[14];
extern const uint8_t gm_si0mnqg6j1eu8[19];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sitlbs33jqqal[2];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sm0f8bs4gd2ma[18];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_sn8alsh9na9ne[12];
extern const uint8_t gm_spi8n2atkg603[17];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0gtuh94b1fce[];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src2j6ljeja7ilp[];
extern const char gm_src4pc97o88riqc[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sv7lsha1e0i8r[13];
extern const uint8_t gm_svpofgjtij7vj[4];
extern const uint8_t gm_svpr02j6er8vd[15];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0lfe1s2pin(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    return true;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqg63o53ar52(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)21) return gm_eqehhdtpeti1(a + 4, b + 4);
    if (ta == (uint8_t)68) return gm_eqov4e3mbi0f(a + 4, b + 4);
    if (ta == (uint8_t)72) return gm_eqtje7cegoml(a + 4, b + 4);
    if (ta == (uint8_t)79) return gm_eq0lfe1s2pin(a + 4, b + 4);
    if (ta == (uint8_t)80) return gm_eq467d5cm1d5(a + 4, b + 4);
    if (ta == (uint8_t)81) return gm_eq467d5cm1d5(a + 4, b + 4);
    return true;
}
inline bool gm_eqov4e3mbi0f(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t3, t4, t6);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_asm_text_fmvci6l(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_asm_text_fmvci6l F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)5ULL)) goto ie2;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 8) + 8));
        gm_l1 = t5;
        {
            gm_b16_8 t7 = gm_l1;
            gm_b16_8 t8 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_trap("reached `@unreachable()`", gm_src2j6ljeja7ilp, 5, 28);
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
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_layout_23rect9 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_bits_layout_u6kkccp((void *)&(*(gm_t57euh612ligr *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_kotvo8t F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_pn2p277 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)4ULL));
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fresh_5kokjmg F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 232));
        uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 480, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 232), t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 360));
        uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 481, 5);
        uint32_t t13 = r11;
        GM_ST(uint32_t, (((uint8_t *)t8) + 360), t13);
    }
    {
        void * t14 = gm_l0;
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 232));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_cty_i2b4e03(gm_cs2lqoac0a5ng4 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ii_cty_i2b4e03 F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK_SHALLOW(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tuglhjjmj2g58, a0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_b16_8 t3 = gm_f_int_cty_s62af0i(t1, t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_ii_exact_g6a19dp(gm_cs2lqoac0a5ng4 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ii_exact_g6a19dp F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK_SHALLOW(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tuglhjjmj2g58, a0);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t r4; bool o5 = __builtin_mul_overflow(t3, ((uint64_t)8ULL), &r4);
    if (o5) gm_trap("integer overflow in `*`", gm_src4pc97o88riqc, 22, 56);
    uint64_t t6 = r4;
    uint8_t t7 = ((t2 == t6));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_ucty_b40h5ot(gm_cs2lqoac0a5ng4 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ii_ucty_b40h5ot F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tuglhjjmj2g58, a0);
    uint64_t t1 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_b16_8 t2 = gm_f_int_cty_s62af0i(((uint8_t)0), t1);
    F.ret = t2;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_17uennd F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_is_unit_n83ttnb(t3, GM_CONV(gm_csguankko4lmhd, t4));
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
    GM_STACK_CHECK_SHALLOW(&F, 126u);
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
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t7;
        {
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t13 = 0;
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
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&(*(gm_t53at80pfdus3 *)gm_out), t3, GM_CONV(gm_csguankko4lmhd, t4));
    goto gm_fdone;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_line_l6u87p9 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b0_1 t3 = gm_f_indent_line_b3mckqa((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 184);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_extend_0cs85gs(t5, t6);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcmok80ho8hh01, 464, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 184);
        gm_tftb1odc739t5 t11 = gm_f_push_t83rg2m(t10, ((uint8_t)10ULL));
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srcmok80ho8hh01, 465, 5);
        gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_loc_u9c0aqb F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t3) + 72));
    gm_b16_8 t5 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t2, t4);
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
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_one_08qg1op F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, ((uint64_t)1ULL));
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bytes_eocneng F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint64_t t3 = GM_LD(uint64_t, ((((((uint8_t *)t2) + 8) + 8) + 0) + 0));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t83rg2m F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_ijtp9lo();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_3cp9o8d(t10, ((uint64_t)1ULL), t12);
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
        uint8_t t21 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t20 + t18 * 1), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_a5ovnrm((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_slice_len_a3383br F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* addr */
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t3877pk7lv748 t1;
    gm_b16_8 t2 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    void * t3 = gm_l0;
    uint64_t t4 = gm_f_ptr_bytes_eocneng(t3);
    GM_ST(uint64_t, (uint8_t *)&t1 + 16, t4);
    gm_tabb710d9mq2e t5;
    gm_f_format_0gjpujh((void *)&t5, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_src0gtuh94b1fce, 291, 72);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_slice_ptr_hojfgr5 F;
    gm_b16_8 gm_l0; /* addr */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_tnpifv20irnph t2;
    GM_ST(gm_b16_8, ((uint8_t *)&t2 + 0), t1);
    gm_tabb710d9mq2e t3;
    gm_f_format_7lru3c2((void *)&t3, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 8, gm_src0gtuh94b1fce, 289, 60);
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t4 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t1, ((uint8_t)1), t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_kk94re3 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t1, ((uint8_t)0), ((uint16_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_8ekedsl F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t6 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t1, t5);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_ty_vgceilu F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_unsupported_531q51r F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        memset((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0, 4);
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_tabb710d9mq2e t4;
        gm_f_format_fsfutor((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcd5arorrf8jkg, 35, 27);
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_tl874t2atnlh3 t7 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0, t6);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16, t7);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_9mijhtc F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t3);
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
__attribute__((aligned(4))) void gm_f_alloc_tag_a71p35s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3) {
    struct gm_fr_alloc_tag_a71p35s F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* loc */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* k */
    gm_b16_8 gm_l9; /* f */
    gm_b16_8 gm_l10; /* fa */
    gm_b16_8 gm_l11; /* fp */
    gm_b16_8 gm_l12; /* fl */
    gm_b16_8 gm_l13; /* line */
    gm_b16_8 gm_l14; /* col */
    GM_STACK_CHECK(&F, 1582u);
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
        gm_b16_8 t4 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t4;
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t7) + 1136) + 1));
        uint8_t t9 = (uint8_t)!t8;
        if (!t9) goto ie5;
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t12);
            goto gm_fdone;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_f_list_epvducg((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            gm_l6 = ((uint64_t)3ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l7;
                uint64_t t16 = gm_l6;
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l7;
                    uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&t20));
                    gm_l8 = t23;
                    {
                        void * t25 = (void *)((uint8_t *)&F.l5);
                        void * t26 = gm_l0;
                        uint64_t t27 = gm_l8;
                        uint64_t t28 = (uintptr_t)t27;
                        uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t28 >= (uintptr_t)(t29)) gm_trap_index(t28, t29, gm_src2j6ljeja7ilp, 260, 41);
                        void * t30 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t28 * 88);
                        gm_t5o0tpv5jka2l t31;
                        gm_f_expr_porbc9t((void *)&t31, (void *)NULL, cx, NULL, t26, t30);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                        if (((((uint8_t *)&t31))[0] != 0)) {
                            gm_t5o0tpv5jka2l t32;
                            GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
                            ((uint8_t *)&t32)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t32 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t31) + 8)));
                            F.ret = t32;
                            goto gm_fret;
                        }
                        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
                        gm_tftb1odc739t5 t34 = gm_f_push_3ef746b(t25, t33);
                        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_src2j6ljeja7ilp, 260, 21);
                        gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 260, 5);
me21:;
                }
lc14:;
                {
                    uint64_t t37 = gm_l7;
                    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                    uint64_t t40 = r38;
                    gm_l7 = t40;
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
        void * t41 = gm_l0;
        uint64_t t42 = (uintptr_t)((uint64_t)3ULL);
        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_src2j6ljeja7ilp, 261, 23);
        void * t44 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t42 * 88);
        gm_t5o0tpv5jka2l t45;
        gm_f_expr_porbc9t((void *)&t45, (void *)NULL, cx, NULL, t41, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t45))[0] != 0)) {
            gm_t5o0tpv5jka2l t46;
            GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
            ((uint8_t *)&t46)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t46 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t45) + 8)));
            F.ret = t46;
            goto gm_fret;
        }
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
        gm_l9 = t47;
    }
    {
        void * t48 = gm_l0;
        uint64_t t49 = (uintptr_t)((uint64_t)3ULL);
        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t49 >= (uintptr_t)(t50)) gm_trap_index(t49, t50, gm_src2j6ljeja7ilp, 262, 22);
        gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t49 * 88) + 0));
        gm_b16_8 t52 = gm_l9;
        gm_b16_8 t53 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t48, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t53;
    }
    {
        gm_b16_8 t54 = gm_l10;
        gm_b16_8 t55 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t55;
    }
    {
        void * t56 = gm_l0;
        gm_b16_8 t57 = gm_l10;
        gm_b16_8 t58 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t56, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t58;
    }
    {
        void * t59 = gm_l0;
        uint64_t t60 = (uintptr_t)((uint64_t)4ULL);
        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t60 >= (uintptr_t)(t61)) gm_trap_index(t60, t61, gm_src2j6ljeja7ilp, 265, 26);
        void * t62 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t60 * 88);
        gm_t5o0tpv5jka2l t63;
        gm_f_expr_porbc9t((void *)&t63, (void *)NULL, cx, NULL, t59, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t63))[0] != 0)) {
            gm_t5o0tpv5jka2l t64;
            GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
            ((uint8_t *)&t64)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t64 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t63) + 8)));
            F.ret = t64;
            goto gm_fret;
        }
        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
        gm_l13 = t65;
    }
    {
        void * t66 = gm_l0;
        uint64_t t67 = (uintptr_t)((uint64_t)5ULL);
        uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_src2j6ljeja7ilp, 266, 25);
        void * t69 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t67 * 88);
        gm_t5o0tpv5jka2l t70;
        gm_f_expr_porbc9t((void *)&t70, (void *)NULL, cx, NULL, t66, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t70))[0] != 0)) {
            gm_t5o0tpv5jka2l t71;
            GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
            ((uint8_t *)&t71)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t71 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t70) + 8)));
            F.ret = t71;
            goto gm_fret;
        }
        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
        gm_l14 = t72;
    }
    {
        uint8_t t74 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)76ULL)));
        if (!t74) goto ie73;
        {
            void * t76 = gm_l0;
            gm_th2d9c7aju8hp t77;
            uint64_t t78 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t79 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_src2j6ljeja7ilp, 268, 63);
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t78 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t77 + 0, t80);
            uint64_t t81 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t82 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            if (t81 >= (uintptr_t)(t82)) gm_trap_index(t81, t82, gm_src2j6ljeja7ilp, 268, 75);
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t81 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t77 + 16, t83);
            uint64_t t84 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t85 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            if (t84 >= (uintptr_t)(t85)) gm_trap_index(t84, t85, gm_src2j6ljeja7ilp, 268, 87);
            gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t84 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t77 + 32, t86);
            gm_b16_8 t87 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t77 + 48, t87);
            gm_b16_8 t88 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t77 + 64, t88);
            gm_b16_8 t89 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t77 + 80, t89);
            gm_b16_8 t90 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t77 + 96, t90);
            gm_b0_1 t91 = gm_f_linef_ki2bckn((void *)NULL, cx, NULL, t76, &t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix73;
ie73:;
        {
            void * t93 = gm_l0;
            gm_tg1t0c5hkovn5 t94;
            uint64_t t95 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t96 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            if (t95 >= (uintptr_t)(t96)) gm_trap_index(t95, t96, gm_src2j6ljeja7ilp, 271, 69);
            gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t95 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 0, t97);
            uint64_t t98 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t99 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            if (t98 >= (uintptr_t)(t99)) gm_trap_index(t98, t99, gm_src2j6ljeja7ilp, 271, 81);
            gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t98 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 16, t100);
            uint64_t t101 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t102 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            if (t101 >= (uintptr_t)(t102)) gm_trap_index(t101, t102, gm_src2j6ljeja7ilp, 271, 93);
            gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t101 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 32, t103);
            gm_b16_8 t104 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 48, t104);
            gm_b16_8 t105 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 64, t105);
            gm_b16_8 t106 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 80, t106);
            gm_b16_8 t107 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 96, t107);
            gm_b16_8 t108 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 112, t108);
            gm_b0_1 t109 = gm_f_linef_evpuk0q((void *)NULL, cx, NULL, t93, &t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix73:;
    }
    {
        void * t110 = gm_l0;
        gm_b16_8 t111 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t110);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t111);
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
__attribute__((aligned(4))) void gm_f_arith_asm_pklrmbh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3) {
    struct gm_fr_arith_asm_pklrmbh F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* args */
    uint64_t gm_l2; /* outputs */
    uint64_t gm_l3; /* inputs */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    void * gm_l8; /* c */
    void * gm_l9; /* p */
    gm_t88rcucg1b7uk gm_l10; /* pointee */
    gm_b16_8 gm_l11; /* ct */
    gm_b16_8 gm_l12; /* pv */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    void * gm_l17; /* c */
    void * gm_l18; /* v */
    gm_b16_8 gm_l19; /* vv */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* k */
    GM_STACK_CHECK(&F, 1762u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = gm_l2;
            gm_l5 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l6;
                uint64_t t6 = gm_l5;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l6;
                    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&t10));
                    gm_l7 = t13;
                    {
                        uint64_t t15 = gm_l7;
                        uint64_t r16; bool o17 = __builtin_mul_overflow(((uint64_t)2ULL), t15, &r16);
                        if (o17) gm_trap("integer overflow in `*`", gm_src2j6ljeja7ilp, 14, 24);
                        uint64_t t18 = r16;
                        uint64_t r19; bool o20 = __builtin_add_overflow(((uint64_t)1ULL), t18, &r19);
                        if (o20) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 14, 20);
                        uint64_t t21 = r19;
                        uint64_t t22 = (uintptr_t)t21;
                        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_src2j6ljeja7ilp, 14, 15);
                        void * t24 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t22 * 88);
                        gm_l8 = t24;
                    }
                    {
                        uint64_t t25 = gm_l7;
                        uint64_t r26; bool o27 = __builtin_mul_overflow(((uint64_t)2ULL), t25, &r26);
                        if (o27) gm_trap("integer overflow in `*`", gm_src2j6ljeja7ilp, 15, 24);
                        uint64_t t28 = r26;
                        uint64_t r29; bool o30 = __builtin_add_overflow(((uint64_t)2ULL), t28, &r29);
                        if (o30) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 15, 20);
                        uint64_t t31 = r29;
                        uint64_t t32 = (uintptr_t)t31;
                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_src2j6ljeja7ilp, 15, 15);
                        void * t34 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 88);
                        gm_l9 = t34;
                    }
                    {
                        void * t36 = gm_l0;
                        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
                        void * t38 = (void *)(((uint8_t *)t37) + 8);
                        void * t39 = gm_l9;
                        gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t39) + 0));
                        gm_t53at80pfdus3 t41;
                        gm_f_kind_ghvav22((void *)&t41, t38, GM_CONV(gm_csguankko4lmhd, t40));
                        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
                        if (t42 != ((uint8_t)8ULL)) goto ie35;
                        gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t41) + 8) + 4));
                        gm_l10 = t43;
                        {
                            void * t45 = gm_l0;
                            void * t46 = GM_LD(void *, (((uint8_t *)t45) + 0));
                            void * t47 = (void *)(((uint8_t *)t46) + 8);
                            gm_t88rcucg1b7uk t48 = gm_l10;
                            gm_b16_8 t49 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t47, t48);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_l11 = t49;
                        }
                        {
                            void * t50 = gm_l0;
                            void * t51 = gm_l9;
                            gm_t5o0tpv5jka2l t52;
                            gm_f_expr_porbc9t((void *)&t52, (void *)NULL, cx, NULL, t50, t51);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            if (((((uint8_t *)&t52))[0] != 0)) {
                                gm_t5o0tpv5jka2l t53;
                                GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
                                ((uint8_t *)&t53)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t53 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t52) + 8)));
                                F.ret = t53;
                                goto gm_fret;
                            }
                            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t52) + 8));
                            gm_l12 = t54;
                        }
                        {
                            void * t55 = (void *)((uint8_t *)&F.l4);
                            gm_tfeab279c16rr t56;
                            void * t57 = gm_l8;
                            gm_b16_8 t58 = gm_f_asm_text_fmvci6l((void *)NULL, cx, NULL, t57);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t56 + 0, t58);
                            gm_b16_8 t59 = gm_l11;
                            GM_ST(gm_b16_8, (uint8_t *)&t56 + 16, t59);
                            gm_b16_8 t60 = gm_l12;
                            GM_ST(gm_b16_8, (uint8_t *)&t56 + 32, t60);
                            gm_tabb710d9mq2e t61;
                            gm_f_format_bm6js5o((void *)&t61, (void *)NULL, cx, NULL, &t56);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 8, gm_src2j6ljeja7ilp, 19, 19);
                            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t61) + 8));
                            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
                            gm_tftb1odc739t5 t64 = gm_f_push_3ef746b(t55, t63);
                            if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_src2j6ljeja7ilp, 19, 9);
                            gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
                        }
                        goto ix35;
ie35:;
                        {
                            gm_trap("reached `@unreachable()`", gm_src2j6ljeja7ilp, 16, 53);
                        }
ix35:;
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 13, 5);
me11:;
                }
lc4:;
                {
                    uint64_t t68 = gm_l6;
                    uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                    uint64_t t71 = r69;
                    gm_l6 = t71;
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
        gm_f_list_epvducg((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            uint64_t t73 = gm_l3;
            gm_l14 = t73;
        }
        {
            for (;;) {
                uint64_t t75 = gm_l15;
                uint64_t t76 = gm_l14;
                uint8_t t77 = (t75 < t76);
                if (!t77) goto lb74;
                {
                    uint64_t t80 = gm_l15;
                    uint64_t t83 = GM_LD(uint64_t, ((uint8_t *)&t80));
                    gm_l16 = t83;
                    {
                        uint64_t t85 = gm_l2;
                        uint64_t t86 = gm_l16;
                        uint64_t r87; bool o88 = __builtin_add_overflow(t85, t86, &r87);
                        if (o88) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 23, 29);
                        uint64_t t89 = r87;
                        uint64_t r90; bool o91 = __builtin_mul_overflow(((uint64_t)2ULL), t89, &r90);
                        if (o91) gm_trap("integer overflow in `*`", gm_src2j6ljeja7ilp, 23, 24);
                        uint64_t t92 = r90;
                        uint64_t r93; bool o94 = __builtin_add_overflow(((uint64_t)1ULL), t92, &r93);
                        if (o94) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 23, 20);
                        uint64_t t95 = r93;
                        uint64_t t96 = (uintptr_t)t95;
                        uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t96 >= (uintptr_t)(t97)) gm_trap_index(t96, t97, gm_src2j6ljeja7ilp, 23, 15);
                        void * t98 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t96 * 88);
                        gm_l17 = t98;
                    }
                    {
                        uint64_t t99 = gm_l2;
                        uint64_t t100 = gm_l16;
                        uint64_t r101; bool o102 = __builtin_add_overflow(t99, t100, &r101);
                        if (o102) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 24, 29);
                        uint64_t t103 = r101;
                        uint64_t r104; bool o105 = __builtin_mul_overflow(((uint64_t)2ULL), t103, &r104);
                        if (o105) gm_trap("integer overflow in `*`", gm_src2j6ljeja7ilp, 24, 24);
                        uint64_t t106 = r104;
                        uint64_t r107; bool o108 = __builtin_add_overflow(((uint64_t)2ULL), t106, &r107);
                        if (o108) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 24, 20);
                        uint64_t t109 = r107;
                        uint64_t t110 = (uintptr_t)t109;
                        uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t110 >= (uintptr_t)(t111)) gm_trap_index(t110, t111, gm_src2j6ljeja7ilp, 24, 15);
                        void * t112 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t110 * 88);
                        gm_l18 = t112;
                    }
                    {
                        void * t113 = gm_l0;
                        void * t114 = gm_l18;
                        gm_t5o0tpv5jka2l t115;
                        gm_f_expr_porbc9t((void *)&t115, (void *)NULL, cx, NULL, t113, t114);
                        if (gm_unw.kind) {
                            goto gm_ll78;
                        }
                        if (((((uint8_t *)&t115))[0] != 0)) {
                            gm_t5o0tpv5jka2l t116;
                            GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
                            ((uint8_t *)&t116)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t116 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t115) + 8)));
                            F.ret = t116;
                            goto gm_fret;
                        }
                        gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&t115) + 8));
                        gm_l19 = t117;
                    }
                    {
                        void * t118 = (void *)((uint8_t *)&F.l13);
                        gm_t6j6smj7qoctu t119;
                        void * t120 = gm_l17;
                        gm_b16_8 t121 = gm_f_asm_text_fmvci6l((void *)NULL, cx, NULL, t120);
                        if (gm_unw.kind) {
                            goto gm_ll78;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t119 + 0, t121);
                        gm_b16_8 t122 = gm_l19;
                        GM_ST(gm_b16_8, (uint8_t *)&t119 + 16, t122);
                        gm_tabb710d9mq2e t123;
                        gm_f_format_nm3fl8o((void *)&t123, (void *)NULL, cx, NULL, &t119);
                        if (gm_unw.kind) {
                            goto gm_ll78;
                        }
                        if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 8, gm_src2j6ljeja7ilp, 26, 18);
                        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 8));
                        gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
                        gm_tftb1odc739t5 t126 = gm_f_push_3ef746b(t118, t125);
                        if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_src2j6ljeja7ilp, 26, 9);
                        gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
                    }
                    goto me81;
mn82:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 22, 5);
me81:;
                }
lc74:;
                {
                    uint64_t t129 = gm_l15;
                    uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                    uint64_t t132 = r130;
                    gm_l15 = t132;
                }
            }
gm_ll78:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb74; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc74; }
            }
            goto gm_lad;
lb74:;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint64_t t134 = gm_l2;
            uint64_t t135 = gm_l3;
            uint64_t r136; bool o137 = __builtin_add_overflow(t134, t135, &r136);
            if (o137) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 29, 24);
            uint64_t t138 = r136;
            uint64_t r139; bool o140 = __builtin_mul_overflow(((uint64_t)2ULL), t138, &r139);
            if (o140) gm_trap("integer overflow in `*`", gm_src2j6ljeja7ilp, 29, 19);
            uint64_t t141 = r139;
            uint64_t r142; bool o143 = __builtin_add_overflow(((uint64_t)1ULL), t141, &r142);
            if (o143) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 29, 15);
            uint64_t t144 = r142;
            gm_l22 = t144;
        }
        {
            uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l21 = t145;
        }
        {
            for (;;) {
                uint64_t t147 = gm_l22;
                uint64_t t148 = gm_l21;
                uint8_t t149 = (t147 < t148);
                if (!t149) goto lb146;
                {
                    uint64_t t152 = gm_l22;
                    uint64_t t155 = GM_LD(uint64_t, ((uint8_t *)&t152));
                    gm_l23 = t155;
                    {
                        void * t157 = (void *)((uint8_t *)&F.l20);
                        uint64_t t158 = gm_l23;
                        uint64_t t159 = (uintptr_t)t158;
                        uint64_t t160 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t159 >= (uintptr_t)(t160)) gm_trap_index(t159, t160, gm_src2j6ljeja7ilp, 29, 79);
                        void * t161 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t159 * 88);
                        gm_b16_8 t162 = gm_f_asm_text_fmvci6l((void *)NULL, cx, NULL, t161);
                        if (gm_unw.kind) {
                            goto gm_ll150;
                        }
                        gm_tftb1odc739t5 t163 = gm_f_push_3ef746b(t157, t162);
                        if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 1, gm_src2j6ljeja7ilp, 29, 55);
                        gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t163) + 1));
                    }
                    goto me153;
mn154:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 29, 5);
me153:;
                }
lc146:;
                {
                    uint64_t t166 = gm_l22;
                    uint64_t r167; bool o168 = __builtin_add_overflow(t166, ((uint64_t)1ULL), &r167);
                    uint64_t t169 = r167;
                    gm_l22 = t169;
                }
            }
gm_ll150:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb146; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc146; }
            }
            goto gm_lad;
lb146:;
        }
    }
    {
        void * t170 = gm_l0;
        gm_t0uuhaljsf410 t171;
        uint64_t t172 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t173 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t172 >= (uintptr_t)(t173)) gm_trap_index(t172, t173, gm_src2j6ljeja7ilp, 30, 65);
        void * t174 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t172 * 88);
        gm_b16_8 t175 = gm_f_asm_text_fmvci6l((void *)NULL, cx, NULL, t174);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t171 + 0, t175);
        gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&t176));
        gm_b16_8 t178;
        GM_ST(void *, (uint8_t *)&t178, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t179;
        gm_f_join_sfknljb((void *)&t179, (void *)NULL, cx, NULL, t177, t178);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 8, gm_src2j6ljeja7ilp, 30, 75);
        gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)&t179) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t171 + 16, t180);
        gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
        gm_b16_8 t183;
        GM_ST(void *, (uint8_t *)&t183, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t184;
        gm_f_join_sfknljb((void *)&t184, (void *)NULL, cx, NULL, t182, t183);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 8, gm_src2j6ljeja7ilp, 30, 104);
        gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)&t184) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t171 + 32, t185);
        gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
        gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&t186));
        gm_b16_8 t188;
        GM_ST(void *, (uint8_t *)&t188, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t189;
        gm_f_join_sfknljb((void *)&t189, (void *)NULL, cx, NULL, t187, t188);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t189))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t189) + 8, gm_src2j6ljeja7ilp, 30, 132);
        gm_b16_8 t190 = GM_LD(gm_b16_8, (((uint8_t *)&t189) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t171 + 48, t190);
        gm_b0_1 t191 = gm_f_linef_81ikvle((void *)NULL, cx, NULL, t170, &t171);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t192 = gm_l0;
        gm_b16_8 t193 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t192);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t193);
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
__attribute__((aligned(4))) void gm_f_arith_get_jao7f8p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3) {
    struct gm_fr_arith_get_jao7f8p F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_t88rcucg1b7uk gm_l4; /* t */
    void * gm_l5; /* base */
    gm_b16_8 gm_l6; /* ptr */
    gm_b16_8 gm_l7; /* len */
    gm_b16_8 gm_l8; /* v */
    gm_b16_8 gm_l9; /* a */
    uint64_t gm_l10; /* n */
    gm_t88rcucg1b7uk gm_l11; /* pointee */
    uint64_t gm_l12; /* n */
    gm_b16_8 gm_l13; /* v */
    void * gm_l14; /* tys */
    gm_b16_8 gm_l15; /* v */
    gm_b16_8 gm_l16; /* i */
    gm_t88rcucg1b7uk gm_l17; /* inner */
    gm_t88rcucg1b7uk gm_l18; /* elem */
    gm_t88rcucg1b7uk gm_l19; /* pointee */
    uint64_t gm_l20; /* esz */
    gm_b16_8 gm_l21; /* res */
    gm_b16_8 gm_l22; /* addr */
    gm_b16_8 gm_l23; /* v */
    gm_b16_8 gm_l24; /* some */
    gm_b16_8 gm_l25; /* none */
    GM_STACK_CHECK(&F, 3192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_src2j6ljeja7ilp, 450, 14);
        void * t6 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t4 * 88);
        gm_l5 = t6;
    }
    {
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(0));
        gm_l6 = t7;
    }
    {
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(0));
        gm_l7 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        void * t11 = (void *)(((uint8_t *)t10) + 8);
        void * t12 = gm_l5;
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t12) + 0));
        gm_t53at80pfdus3 t14;
        gm_f_kind_ghvav22((void *)&t14, t11, GM_CONV(gm_csguankko4lmhd, t13));
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        switch (t21) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma19; case ((uint8_t)3ULL): goto ma19; case ((uint8_t)4ULL): goto ma19; case ((uint8_t)5ULL): goto ma19; case ((uint8_t)6ULL): goto ma19; case ((uint8_t)7ULL): goto ma19; case ((uint8_t)8ULL): goto ma18; case ((uint8_t)9ULL): goto ma19; case ((uint8_t)10ULL): goto ma16; case ((uint8_t)11ULL): goto ma17; case ((uint8_t)12ULL): goto ma19; case ((uint8_t)13ULL): goto ma19; case ((uint8_t)14ULL): goto ma19; case ((uint8_t)15ULL): goto ma19; case ((uint8_t)16ULL): goto ma19; case ((uint8_t)17ULL): goto ma19; case ((uint8_t)18ULL): goto ma19; case ((uint8_t)19ULL): goto ma19; case ((uint8_t)20ULL): goto ma19; case ((uint8_t)21ULL): goto ma19; case ((uint8_t)22ULL): goto ma19; case ((uint8_t)23ULL): goto ma19; case ((uint8_t)24ULL): goto ma19; case ((uint8_t)25ULL): goto ma19; case ((uint8_t)26ULL): goto ma19; default: goto ma19; }
ma16:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t23 != ((uint8_t)10ULL)) goto mn22;
        {
            void * t25 = gm_l0;
            void * t26 = gm_l5;
            gm_t5o0tpv5jka2l t27;
            gm_f_expr_porbc9t((void *)&t27, (void *)NULL, cx, NULL, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t27))[0] != 0)) {
                gm_t5o0tpv5jka2l t28;
                GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
                ((uint8_t *)&t28)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t28 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t27) + 8)));
                F.ret = t28;
                goto gm_fret;
            }
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
            gm_l8 = t29;
        }
        {
            void * t30 = gm_l0;
            void * t31 = gm_l5;
            gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t31) + 0));
            gm_b16_8 t33 = gm_l8;
            gm_b16_8 t34 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t30, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t34;
        }
        {
            gm_b16_8 t35 = gm_l9;
            gm_b16_8 t36 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t37;
            GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
            gm_tabb710d9mq2e t38;
            gm_f_format_da6unq6((void *)&t38, (void *)NULL, cx, NULL, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 8, gm_src2j6ljeja7ilp, 457, 19);
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 8));
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            gm_l6 = t40;
        }
        {
            void * t41 = gm_l0;
            gm_b16_8 t42 = gm_l9;
            gm_b16_8 t43 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t43;
        }
        goto me15;
mn22:;
ma17:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t45 != ((uint8_t)11ULL)) goto mn44;
        uint64_t t46 = GM_LD(uint64_t, ((((uint8_t *)&t14) + 8) + 0));
        gm_l10 = t46;
        {
            void * t48 = gm_l0;
            void * t49 = gm_l5;
            gm_t5o0tpv5jka2l t50;
            gm_f_place_addr_7ji15ot((void *)&t50, (void *)NULL, cx, NULL, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t50))[0] != 0)) {
                gm_t5o0tpv5jka2l t51;
                GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                ((uint8_t *)&t51)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t51 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t50) + 8)));
                F.ret = t51;
                goto gm_fret;
            }
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
            gm_l6 = t52;
        }
        {
            uint64_t t53 = gm_l10;
            gm_t4ce0jd3tdro4 t54;
            GM_ST(uint64_t, ((uint8_t *)&t54 + 0), t53);
            gm_tabb710d9mq2e t55;
            gm_f_format_1amnh72((void *)&t55, (void *)NULL, cx, NULL, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_src2j6ljeja7ilp, 462, 19);
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
            gm_l7 = t57;
        }
        goto me15;
mn44:;
ma18:;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t59 != ((uint8_t)8ULL)) goto mn58;
        gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t14) + 8) + 4));
        gm_l11 = t60;
        void * t61 = gm_l0;
        void * t62 = GM_LD(void *, (((uint8_t *)t61) + 0));
        void * t63 = (void *)(((uint8_t *)t62) + 8);
        gm_t88rcucg1b7uk t64 = gm_l11;
        gm_t53at80pfdus3 t65;
        gm_f_kind_ghvav22((void *)&t65, t63, GM_CONV(gm_csguankko4lmhd, t64));
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t65) + 0));
        if (t68 != ((uint8_t)11ULL)) goto mn67;
        uint64_t t69 = GM_LD(uint64_t, ((((uint8_t *)&t65) + 8) + 0));
        gm_l12 = t69;
        {
            void * t71 = gm_l0;
            void * t72 = gm_l5;
            gm_t5o0tpv5jka2l t73;
            gm_f_expr_porbc9t((void *)&t73, (void *)NULL, cx, NULL, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t73))[0] != 0)) {
                gm_t5o0tpv5jka2l t74;
                GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                ((uint8_t *)&t74)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t74 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t73) + 8)));
                F.ret = t74;
                goto gm_fret;
            }
            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
            gm_l13 = t75;
        }
        {
            gm_b16_8 t76 = gm_l13;
            gm_tnpifv20irnph t77;
            GM_ST(gm_b16_8, ((uint8_t *)&t77 + 0), t76);
            gm_tabb710d9mq2e t78;
            gm_f_format_da6unq6((void *)&t78, (void *)NULL, cx, NULL, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 8, gm_src2j6ljeja7ilp, 467, 23);
            gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t78) + 8));
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
            gm_l6 = t80;
        }
        {
            uint64_t t81 = gm_l12;
            gm_t4ce0jd3tdro4 t82;
            GM_ST(uint64_t, ((uint8_t *)&t82 + 0), t81);
            gm_tabb710d9mq2e t83;
            gm_f_format_1amnh72((void *)&t83, (void *)NULL, cx, NULL, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 8, gm_src2j6ljeja7ilp, 468, 23);
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t83) + 8));
            gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
            gm_l7 = t85;
        }
        goto me66;
mn67:;
        void * t87 = gm_l1;
        gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + 72));
        gm_b16_8 t89;
        GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sd2ht7fkpnftp));
        GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(16));
        gm_t08olq97rscn3 t90;
        gm_f_unsupported_531q51r((void *)&t90, (void *)NULL, cx, NULL, t88, t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t90);
        goto gm_fdone;
        goto me66;
mn86:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 464, 29);
me66:;
        goto me15;
mn58:;
ma19:;
        void * t92 = gm_l1;
        gm_tl874t2atnlh3 t93 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t92) + 72));
        void * t94 = gm_l0;
        void * t95 = GM_LD(void *, (((uint8_t *)t94) + 0));
        void * t96 = (void *)(((uint8_t *)t95) + 8);
        void * t97 = gm_l5;
        gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t97) + 0));
        gm_b16_8 t99 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t96, t98);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t100;
        GM_ST(gm_b16_8, ((uint8_t *)&t100 + 0), t99);
        gm_tabb710d9mq2e t101;
        gm_f_format_4ok6i29((void *)&t101, (void *)NULL, cx, NULL, t100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 8, gm_src2j6ljeja7ilp, 472, 42);
        gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&t101) + 8));
        gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
        gm_t08olq97rscn3 t104;
        gm_f_unsupported_531q51r((void *)&t104, (void *)NULL, cx, NULL, t93, t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t104);
        goto gm_fdone;
        goto me15;
mn91:;
ma20:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 453, 5);
me15:;
    }
    {
        void * t105 = gm_l0;
        void * t106 = GM_LD(void *, (((uint8_t *)t105) + 0));
        void * t107 = GM_LD(void *, ((((uint8_t *)t106) + 8) + 0));
        void * t108 = (void *)(((uint8_t *)t107) + 8);
        gm_l14 = t108;
    }
    {
        void * t109 = gm_l0;
        void * t110 = gm_l14;
        void * t111 = gm_l14;
        gm_t88rcucg1b7uk t112 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t113 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t110, ((uint8_t)0), t112);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t114 = gm_l6;
        gm_b16_8 t115 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t109, t113, t114);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t115;
    }
    {
        void * t116 = gm_l0;
        void * t117 = gm_l0;
        void * t118 = GM_LD(void *, (((uint8_t *)t117) + 0));
        void * t119 = (void *)(((uint8_t *)t118) + 8);
        gm_t88rcucg1b7uk t120 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t119);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t121 = gm_l7;
        gm_tnpifv20irnph t122;
        GM_ST(gm_b16_8, ((uint8_t *)&t122 + 0), t121);
        gm_tabb710d9mq2e t123;
        gm_f_format_6df13cd((void *)&t123, (void *)NULL, cx, NULL, t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 8, gm_src2j6ljeja7ilp, 476, 38);
        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 8));
        gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
        gm_b16_8 t126 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t116, t120, t125);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t126;
    }
    {
        gm_b16_8 t127;
        uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t133) { case ((uint8_t)0ULL): goto ma131; case ((uint8_t)1ULL): goto ma131; case ((uint8_t)2ULL): goto ma131; case ((uint8_t)3ULL): goto ma131; case ((uint8_t)4ULL): goto ma131; case ((uint8_t)5ULL): goto ma131; case ((uint8_t)6ULL): goto ma131; case ((uint8_t)7ULL): goto ma131; case ((uint8_t)8ULL): goto ma131; case ((uint8_t)9ULL): goto ma131; case ((uint8_t)10ULL): goto ma131; case ((uint8_t)11ULL): goto ma131; case ((uint8_t)12ULL): goto ma131; case ((uint8_t)13ULL): goto ma131; case ((uint8_t)14ULL): goto ma131; case ((uint8_t)15ULL): goto ma131; case ((uint8_t)16ULL): goto ma131; case ((uint8_t)17ULL): goto ma131; case ((uint8_t)18ULL): goto ma131; case ((uint8_t)19ULL): goto ma131; case ((uint8_t)20ULL): goto ma131; case ((uint8_t)21ULL): goto ma131; case ((uint8_t)22ULL): goto ma131; case ((uint8_t)23ULL): goto ma131; case ((uint8_t)24ULL): goto ma131; case ((uint8_t)25ULL): goto ma131; case ((uint8_t)26ULL): goto ma131; case ((uint8_t)27ULL): goto ma131; case ((uint8_t)28ULL): goto ma131; case ((uint8_t)29ULL): goto ma131; case ((uint8_t)30ULL): goto ma131; case ((uint8_t)31ULL): goto ma131; case ((uint8_t)32ULL): goto ma131; case ((uint8_t)33ULL): goto ma131; case ((uint8_t)34ULL): goto ma131; case ((uint8_t)35ULL): goto ma131; case ((uint8_t)36ULL): goto ma131; case ((uint8_t)37ULL): goto ma131; case ((uint8_t)38ULL): goto ma129; case ((uint8_t)39ULL): goto ma130; case ((uint8_t)40ULL): goto ma131; case ((uint8_t)41ULL): goto ma131; case ((uint8_t)42ULL): goto ma131; case ((uint8_t)43ULL): goto ma131; case ((uint8_t)44ULL): goto ma131; case ((uint8_t)45ULL): goto ma131; case ((uint8_t)46ULL): goto ma131; case ((uint8_t)47ULL): goto ma131; case ((uint8_t)48ULL): goto ma131; case ((uint8_t)49ULL): goto ma131; case ((uint8_t)50ULL): goto ma131; case ((uint8_t)51ULL): goto ma131; case ((uint8_t)52ULL): goto ma131; case ((uint8_t)53ULL): goto ma131; case ((uint8_t)54ULL): goto ma131; case ((uint8_t)55ULL): goto ma131; case ((uint8_t)56ULL): goto ma131; case ((uint8_t)57ULL): goto ma131; case ((uint8_t)58ULL): goto ma131; case ((uint8_t)59ULL): goto ma131; case ((uint8_t)60ULL): goto ma131; case ((uint8_t)61ULL): goto ma131; case ((uint8_t)62ULL): goto ma131; case ((uint8_t)63ULL): goto ma131; case ((uint8_t)64ULL): goto ma131; case ((uint8_t)65ULL): goto ma131; case ((uint8_t)66ULL): goto ma131; case ((uint8_t)67ULL): goto ma131; case ((uint8_t)68ULL): goto ma131; case ((uint8_t)69ULL): goto ma131; case ((uint8_t)70ULL): goto ma131; case ((uint8_t)71ULL): goto ma131; case ((uint8_t)72ULL): goto ma131; case ((uint8_t)73ULL): goto ma131; case ((uint8_t)74ULL): goto ma131; case ((uint8_t)75ULL): goto ma131; case ((uint8_t)76ULL): goto ma131; case ((uint8_t)77ULL): goto ma131; case ((uint8_t)78ULL): goto ma131; case ((uint8_t)79ULL): goto ma131; case ((uint8_t)80ULL): goto ma131; case ((uint8_t)81ULL): goto ma131; default: goto ma131; }
ma129:;
        uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t135 != ((uint8_t)38ULL)) goto mn134;
        gm_b16_8 t136;
        GM_ST(void *, (uint8_t *)&t136, (void *)(gm_s0fg7tcl0p4ij));
        GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(12));
        t127 = t136;
        goto me128;
mn134:;
ma130:;
        uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t138 != ((uint8_t)39ULL)) goto mn137;
        gm_b16_8 t139 = gm_l7;
        gm_tnpifv20irnph t140;
        GM_ST(gm_b16_8, ((uint8_t *)&t140 + 0), t139);
        gm_tabb710d9mq2e t141;
        gm_f_format_uj77khu((void *)&t141, (void *)NULL, cx, NULL, t140);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 8, gm_src2j6ljeja7ilp, 479, 18);
        gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&t141) + 8));
        gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
        t127 = t143;
        goto me128;
mn137:;
ma131:;
        {
            void * t146 = gm_l0;
            uint64_t t147 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t147 >= (uintptr_t)(t148)) gm_trap_index(t147, t148, gm_src2j6ljeja7ilp, 481, 31);
            void * t149 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t147 * 88);
            gm_t5o0tpv5jka2l t150;
            gm_f_expr_porbc9t((void *)&t150, (void *)NULL, cx, NULL, t146, t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t150))[0] != 0)) {
                gm_t5o0tpv5jka2l t151;
                GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
                ((uint8_t *)&t151)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t151 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t150) + 8)));
                F.ret = t151;
                goto gm_fret;
            }
            gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&t150) + 8));
            gm_l15 = t152;
        }
        void * t153 = gm_l0;
        uint64_t t154 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t155 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t154 >= (uintptr_t)(t155)) gm_trap_index(t154, t155, gm_src2j6ljeja7ilp, 482, 22);
        gm_t88rcucg1b7uk t156 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t154 * 88) + 0));
        gm_b16_8 t157 = gm_l15;
        gm_b16_8 t158 = gm_f_bound_n4p0vpb((void *)NULL, cx, NULL, t153, t156, t157);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t159 = t158;
        t127 = t159;
        goto me128;
mn144:;
ma132:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 477, 17);
me128:;
        gm_l16 = t127;
    }
    {
        void * t160 = gm_l0;
        void * t161 = gm_l0;
        void * t162 = GM_LD(void *, (((uint8_t *)t161) + 0));
        void * t163 = (void *)(((uint8_t *)t162) + 8);
        gm_t88rcucg1b7uk t164 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t163);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t165 = gm_l16;
        gm_b16_8 t166 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t160, t164, t165);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t166;
    }
    {
        void * t168 = gm_l0;
        void * t169 = GM_LD(void *, (((uint8_t *)t168) + 0));
        void * t170 = (void *)(((uint8_t *)t169) + 8);
        gm_t88rcucg1b7uk t171 = gm_l4;
        gm_t53at80pfdus3 t172;
        gm_f_kind_ghvav22((void *)&t172, t170, GM_CONV(gm_csguankko4lmhd, t171));
        uint8_t t173 = GM_LD(uint8_t, (((uint8_t *)&t172) + 0));
        if (t173 != ((uint8_t)13ULL)) goto ie167;
        gm_t88rcucg1b7uk t174 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t172) + 8));
        gm_l17 = t174;
        {
            gm_t88rcucg1b7uk t176 = gm_l17;
            gm_l18 = t176;
        }
        {
            uint8_t t178 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)37ULL)));
            if (!t178) goto ie177;
            {
                void * t181 = gm_l0;
                void * t182 = GM_LD(void *, (((uint8_t *)t181) + 0));
                void * t183 = (void *)(((uint8_t *)t182) + 8);
                gm_t88rcucg1b7uk t184 = gm_l17;
                gm_t53at80pfdus3 t185;
                gm_f_kind_ghvav22((void *)&t185, t183, GM_CONV(gm_csguankko4lmhd, t184));
                uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&t185) + 0));
                if (t186 != ((uint8_t)8ULL)) goto ie180;
                gm_t88rcucg1b7uk t187 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t185) + 8) + 4));
                gm_l19 = t187;
                {
                    gm_t88rcucg1b7uk t189 = gm_l19;
                    gm_l18 = t189;
                }
                goto ix180;
ie180:;
ix180:;
            }
            goto ix177;
ie177:;
ix177:;
        }
        {
            void * t190 = gm_l0;
            void * t191 = GM_LD(void *, (((uint8_t *)t190) + 0));
            void * t192 = (void *)(((uint8_t *)t191) + 8);
            gm_t88rcucg1b7uk t193 = gm_l18;
            uint64_t t194 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t192, t193);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t194;
        }
        {
            void * t195 = gm_l0;
            gm_t88rcucg1b7uk t196 = gm_l4;
            gm_b16_8 t197 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t195, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l21 = t197;
        }
        {
            void * t198 = gm_l0;
            gm_t6j6smj7qoctu t199;
            gm_b16_8 t200 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t199 + 0, t200);
            gm_b16_8 t201 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t199 + 16, t201);
            gm_b0_1 t202 = gm_f_linef_lm0b3ih((void *)NULL, cx, NULL, t198, &t199);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tftksr4op114e t203;
            gm_b16_8 t204 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t203 + 0, t204);
            gm_b16_8 t205 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t203 + 16, t205);
            uint64_t t206 = gm_l20;
            GM_ST(uint64_t, (uint8_t *)&t203 + 32, t206);
            gm_tabb710d9mq2e t207;
            gm_f_format_8sn6h96((void *)&t207, (void *)NULL, cx, NULL, &t203);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t207))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t207) + 8, gm_src2j6ljeja7ilp, 494, 13);
            gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&t207) + 8));
            gm_l22 = t208;
        }
        {
            gm_b16_8 t209;
            uint8_t t211 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)37ULL)));
            if (!t211) goto ie210;
            gm_b16_8 t213 = gm_l22;
            gm_tnpifv20irnph t214;
            GM_ST(gm_b16_8, ((uint8_t *)&t214 + 0), t213);
            gm_tabb710d9mq2e t215;
            gm_f_format_2g08abe((void *)&t215, (void *)NULL, cx, NULL, t214);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t215))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t215) + 8, gm_src2j6ljeja7ilp, 495, 36);
            gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&t215) + 8));
            gm_b16_8 t217 = t216;
            gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&t217));
            t209 = t218;
            goto ix210;
ie210:;
            void * t220 = gm_l0;
            gm_t88rcucg1b7uk t221 = gm_l17;
            gm_b16_8 t222 = gm_l22;
            gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
            gm_b16_8 t224 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t220, t221, t223);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t225 = t224;
            t209 = t225;
ix210:;
            gm_l23 = t209;
        }
        {
            void * t226 = gm_l0;
            gm_t88rcucg1b7uk t227 = gm_l4;
            gm_b16_8 t228 = gm_l23;
            gm_b16_8 t229 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t226, t227, t228);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t229;
        }
        {
            void * t230 = gm_l0;
            gm_t6j6smj7qoctu t231;
            gm_b16_8 t232 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t231 + 0, t232);
            gm_b16_8 t233 = gm_l24;
            GM_ST(gm_b16_8, (uint8_t *)&t231 + 16, t233);
            gm_b0_1 t234 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t230, &t231);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t235 = gm_l0;
            gm_b16_8 t236;
            GM_ST(void *, (uint8_t *)&t236, (void *)(gm_s02uuedsru1qu));
            GM_ST(uintptr_t, (uint8_t *)&t236 + 8, (uintptr_t)(8));
            gm_b0_1 t237 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t235, t236);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t238 = gm_l0;
            gm_t88rcucg1b7uk t239 = gm_l4;
            gm_b16_8 t240 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t238, t239);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t240;
        }
        {
            void * t241 = gm_l0;
            gm_t6j6smj7qoctu t242;
            gm_b16_8 t243 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t242 + 0, t243);
            gm_b16_8 t244 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t242 + 16, t244);
            gm_b0_1 t245 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t241, &t242);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t246 = gm_l0;
            gm_b16_8 t247;
            GM_ST(void *, (uint8_t *)&t247, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(1));
            gm_b0_1 t248 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t246, t247);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t249 = gm_l21;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t249);
            goto gm_fdone;
        }
        goto ix167;
ie167:;
        {
            void * t251 = gm_l1;
            gm_tl874t2atnlh3 t252 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t251) + 72));
            gm_b16_8 t253;
            GM_ST(void *, (uint8_t *)&t253, (void *)(gm_sbb1ri5er9djq));
            GM_ST(uintptr_t, (uint8_t *)&t253 + 8, (uintptr_t)(3));
            gm_t08olq97rscn3 t254;
            gm_f_unsupported_531q51r((void *)&t254, (void *)NULL, cx, NULL, t252, t253);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t254);
            goto gm_fdone;
        }
ix167:;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_asm_text_fmvci6l(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_assert_m8ussus(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_assert_m8ussus F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l3; /* loc */
    gm_b16_8 gm_l4; /* c */
    gm_b16_8 gm_l5; /* m */
    gm_b16_8 gm_l6; /* a */
    gm_b16_8 gm_l7; /* p */
    gm_b16_8 gm_l8; /* l */
    gm_b16_8 gm_l9; /* prefix */
    GM_STACK_CHECK(&F, 998u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_b16_8 t4 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t4;
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t7) + 1136) + 0));
        uint8_t t9 = (uint8_t)!t8;
        if (!t9) goto ie5;
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t12);
            goto gm_fdone;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t13 = gm_l0;
        uint64_t t14 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src2j6ljeja7ilp, 202, 23);
        void * t16 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t14 * 88);
        gm_t5o0tpv5jka2l t17;
        gm_f_expr_porbc9t((void *)&t17, (void *)NULL, cx, NULL, t13, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t17))[0] != 0)) {
            gm_t5o0tpv5jka2l t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t18 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t17) + 8)));
            F.ret = t18;
            goto gm_fret;
        }
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
        gm_l4 = t19;
    }
    {
        void * t20 = gm_l0;
        gm_b16_8 t21 = gm_l4;
        gm_tnpifv20irnph t22;
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t21);
        gm_b0_1 t23 = gm_f_linef_o1esdvv((void *)NULL, cx, NULL, t20, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t24 = gm_l0;
        void * t25 = gm_l0;
        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 224));
        uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
        if (o28) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 204, 5);
        uint64_t t29 = r27;
        GM_ST(uint64_t, (((uint8_t *)t24) + 224), t29);
    }
    {
        void * t30 = gm_l0;
        uint64_t t31 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_src2j6ljeja7ilp, 205, 23);
        void * t33 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t31 * 88);
        gm_t5o0tpv5jka2l t34;
        gm_f_expr_porbc9t((void *)&t34, (void *)NULL, cx, NULL, t30, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t34))[0] != 0)) {
            gm_t5o0tpv5jka2l t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t35 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t34) + 8)));
            F.ret = t35;
            goto gm_fret;
        }
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
        gm_l5 = t36;
    }
    {
        void * t37 = gm_l0;
        uint64_t t38 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_src2j6ljeja7ilp, 206, 21);
        gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t38 * 88) + 0));
        gm_b16_8 t41 = gm_l5;
        gm_b16_8 t42 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t37, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t42;
    }
    {
        gm_b16_8 t43 = gm_l6;
        gm_b16_8 t44 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t44;
    }
    {
        void * t45 = gm_l0;
        gm_b16_8 t46 = gm_l6;
        gm_b16_8 t47 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t47;
    }
    {
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_spi8n2atkg603));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(16));
        gm_l9 = t48;
    }
    {
        void * t49 = gm_l0;
        gm_t6q08imqe2768 t50;
        gm_b16_8 t51 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 0, t51);
        gm_b16_8 t52 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 16, t52);
        uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        GM_ST(uint64_t, (uint8_t *)&t50 + 32, t53);
        gm_b16_8 t54 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 40, t54);
        uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        GM_ST(uint64_t, (uint8_t *)&t50 + 56, t55);
        gm_b16_8 t56 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 64, t56);
        uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)2ULL), &r58);
        if (o59) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 210, 298);
        uint64_t t60 = r58;
        GM_ST(uint64_t, (uint8_t *)&t50 + 80, t60);
        gm_b16_8 t61 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 88, t61);
        gm_b0_1 t62 = gm_f_linef_098d9va((void *)NULL, cx, NULL, t49, &t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t63 = gm_l0;
        void * t64 = gm_l0;
        uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)t64) + 224));
        uint64_t r66; bool o67 = __builtin_sub_overflow(t65, ((uint64_t)1ULL), &r66);
        if (o67) gm_trap("integer overflow in `-`", gm_src2j6ljeja7ilp, 211, 5);
        uint64_t t68 = r66;
        GM_ST(uint64_t, (((uint8_t *)t63) + 224), t68);
    }
    {
        void * t69 = gm_l0;
        gm_b16_8 t70;
        GM_ST(void *, (uint8_t *)&t70, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(1));
        gm_b0_1 t71 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t69, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t72 = gm_l0;
        gm_b16_8 t73 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t73);
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
__attribute__((aligned(4))) void gm_f_atomic_plj0t12(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_atomic_plj0t12 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* v0 */
    gm_t88rcucg1b7uk gm_l7; /* at */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    gm_b16_8 gm_l9; /* c */
    gm_b16_8 gm_l10; /* p */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    void * gm_l15; /* x */
    gm_b16_8 gm_l16; /* a */
    gm_b16_8 gm_l17; /* exp */
    gm_b16_8 gm_l18; /* ok */
    gm_b16_8 gm_l19; /* res */
    gm_b16_8 gm_l20; /* none */
    gm_b16_8 gm_l21; /* some */
    gm_t9qkh0rsooc8d gm_l22; /* rmw */
    gm_th2lqbo77pvsc gm_l23; /* f */
    gm_b16_8 gm_l24; /* fname */
    gm_b16_8 gm_l25; /* cmp */
    gm_b16_8 gm_l26; /* old */
    gm_b16_8 gm_l27; /* v */
    GM_STACK_CHECK(&F, 3569u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l5 = t3;
    }
    {
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_src2j6ljeja7ilp, 367, 11);
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t4 * 16));
        gm_l6 = t6;
    }
    {
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_src2j6ljeja7ilp, 368, 11);
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t7 * 88) + 0));
        gm_l7 = t9;
    }
    {
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        void * t13 = (void *)(((uint8_t *)t12) + 8);
        gm_t88rcucg1b7uk t14 = gm_l7;
        gm_t53at80pfdus3 t15;
        gm_f_kind_ghvav22((void *)&t15, t13, GM_CONV(gm_csguankko4lmhd, t14));
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        if (t16 != ((uint8_t)8ULL)) goto ie10;
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t15) + 8) + 4));
        gm_l8 = t17;
        {
            void * t19 = gm_l0;
            void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
            void * t21 = (void *)(((uint8_t *)t20) + 8);
            gm_t88rcucg1b7uk t22 = gm_l8;
            gm_b16_8 t23 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t23;
        }
        {
            gm_t6j6smj7qoctu t24;
            gm_b16_8 t25 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t24 + 0, t25);
            gm_b16_8 t26 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t24 + 16, t26);
            gm_tabb710d9mq2e t27;
            gm_f_format_8m4h4ip((void *)&t27, (void *)NULL, cx, NULL, &t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_src2j6ljeja7ilp, 371, 10);
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
            gm_l10 = t28;
        }
        {
            gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l13 = ((uint64_t)1ULL);
            }
            {
                uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l12 = t30;
            }
            {
                for (;;) {
                    uint64_t t32 = gm_l13;
                    uint64_t t33 = gm_l12;
                    uint8_t t34 = (t32 < t33);
                    if (!t34) goto lb31;
                    {
                        uint64_t t37 = gm_l13;
                        uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&t37));
                        gm_l14 = t40;
                        {
                            uint64_t t42 = gm_l14;
                            uint64_t t43 = (uintptr_t)t42;
                            uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_src2j6ljeja7ilp, 374, 15);
                            void * t45 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t43 * 88);
                            gm_l15 = t45;
                        }
                        {
                            void * t47 = gm_l0;
                            void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
                            void * t49 = (void *)(((uint8_t *)t48) + 8);
                            void * t50 = gm_l15;
                            gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t50) + 0));
                            gm_t53at80pfdus3 t52;
                            gm_f_kind_ghvav22((void *)&t52, t49, GM_CONV(gm_csguankko4lmhd, t51));
                            uint8_t t53;
                            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t52) + 0));
                            if (t56 != ((uint8_t)17ULL)) goto mn55;
                            t53 = ((uint8_t)0);
                            goto me54;
mn55:;
                            t53 = ((uint8_t)1);
                            goto me54;
mn57:;
                            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 375, 12);
me54:;
                            if (!t53) goto ie46;
                            {
                                goto lc31;
                            }
                            goto ix46;
ie46:;
ix46:;
                        }
                        {
                            void * t59 = gm_l0;
                            void * t60 = gm_l15;
                            gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t60) + 0));
                            uint64_t t62 = gm_l14;
                            uint64_t t63 = (uintptr_t)t62;
                            uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            if (t63 >= (uintptr_t)(t64)) gm_trap_index(t63, t64, gm_src2j6ljeja7ilp, 376, 31);
                            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t63 * 16));
                            gm_b16_8 t66 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t59, t61, t65);
                            if (gm_unw.kind) {
                                goto gm_ll35;
                            }
                            gm_l16 = t66;
                        }
                        {
                            void * t67 = (void *)((uint8_t *)&F.l11);
                            gm_b16_8 t68 = gm_l16;
                            gm_tnpifv20irnph t69;
                            GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t68);
                            gm_tabb710d9mq2e t70;
                            gm_f_format_8d9jpvv((void *)&t70, (void *)NULL, cx, NULL, t69);
                            if (gm_unw.kind) {
                                goto gm_ll35;
                            }
                            if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 8, gm_src2j6ljeja7ilp, 377, 19);
                            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
                            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                            gm_tftb1odc739t5 t73 = gm_f_push_3ef746b(t67, t72);
                            if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_src2j6ljeja7ilp, 377, 9);
                            gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
                        }
                        goto me38;
mn39:;
                        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 373, 5);
me38:;
                    }
lc31:;
                    {
                        uint64_t t76 = gm_l13;
                        uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
                        uint64_t t79 = r77;
                        gm_l13 = t79;
                    }
                }
gm_ll35:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb31; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc31; }
                }
                goto gm_lad;
lb31:;
            }
        }
        {
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t90) { case ((uint8_t)0ULL): goto ma88; case ((uint8_t)1ULL): goto ma88; case ((uint8_t)2ULL): goto ma88; case ((uint8_t)3ULL): goto ma88; case ((uint8_t)4ULL): goto ma88; case ((uint8_t)5ULL): goto ma88; case ((uint8_t)6ULL): goto ma88; case ((uint8_t)7ULL): goto ma88; case ((uint8_t)8ULL): goto ma88; case ((uint8_t)9ULL): goto ma88; case ((uint8_t)10ULL): goto ma88; case ((uint8_t)11ULL): goto ma88; case ((uint8_t)12ULL): goto ma88; case ((uint8_t)13ULL): goto ma88; case ((uint8_t)14ULL): goto ma88; case ((uint8_t)15ULL): goto ma88; case ((uint8_t)16ULL): goto ma88; case ((uint8_t)17ULL): goto ma88; case ((uint8_t)18ULL): goto ma88; case ((uint8_t)19ULL): goto ma88; case ((uint8_t)20ULL): goto ma88; case ((uint8_t)21ULL): goto ma88; case ((uint8_t)22ULL): goto ma88; case ((uint8_t)23ULL): goto ma88; case ((uint8_t)24ULL): goto ma88; case ((uint8_t)25ULL): goto ma88; case ((uint8_t)26ULL): goto ma88; case ((uint8_t)27ULL): goto ma88; case ((uint8_t)28ULL): goto ma88; case ((uint8_t)29ULL): goto ma88; case ((uint8_t)30ULL): goto ma88; case ((uint8_t)31ULL): goto ma88; case ((uint8_t)32ULL): goto ma88; case ((uint8_t)33ULL): goto ma88; case ((uint8_t)34ULL): goto ma88; case ((uint8_t)35ULL): goto ma88; case ((uint8_t)36ULL): goto ma88; case ((uint8_t)37ULL): goto ma88; case ((uint8_t)38ULL): goto ma88; case ((uint8_t)39ULL): goto ma88; case ((uint8_t)40ULL): goto ma88; case ((uint8_t)41ULL): goto ma88; case ((uint8_t)42ULL): goto ma88; case ((uint8_t)43ULL): goto ma88; case ((uint8_t)44ULL): goto ma88; case ((uint8_t)45ULL): goto ma88; case ((uint8_t)46ULL): goto ma88; case ((uint8_t)47ULL): goto ma88; case ((uint8_t)48ULL): goto ma88; case ((uint8_t)49ULL): goto ma88; case ((uint8_t)50ULL): goto ma88; case ((uint8_t)51ULL): goto ma88; case ((uint8_t)52ULL): goto ma88; case ((uint8_t)53ULL): goto ma88; case ((uint8_t)54ULL): goto ma88; case ((uint8_t)55ULL): goto ma88; case ((uint8_t)56ULL): goto ma88; case ((uint8_t)57ULL): goto ma88; case ((uint8_t)58ULL): goto ma88; case ((uint8_t)59ULL): goto ma88; case ((uint8_t)60ULL): goto ma88; case ((uint8_t)61ULL): goto ma88; case ((uint8_t)62ULL): goto ma88; case ((uint8_t)63ULL): goto ma88; case ((uint8_t)64ULL): goto ma83; case ((uint8_t)65ULL): goto ma84; case ((uint8_t)66ULL): goto ma85; case ((uint8_t)67ULL): goto ma86; case ((uint8_t)68ULL): goto ma87; case ((uint8_t)69ULL): goto ma88; case ((uint8_t)70ULL): goto ma81; case ((uint8_t)71ULL): goto ma82; case ((uint8_t)72ULL): goto ma88; case ((uint8_t)73ULL): goto ma88; case ((uint8_t)74ULL): goto ma88; case ((uint8_t)75ULL): goto ma88; case ((uint8_t)76ULL): goto ma88; case ((uint8_t)77ULL): goto ma88; case ((uint8_t)78ULL): goto ma88; case ((uint8_t)79ULL): goto ma88; case ((uint8_t)80ULL): goto ma88; case ((uint8_t)81ULL): goto ma88; default: goto ma88; }
ma81:;
            uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t92 != ((uint8_t)70ULL)) goto mn91;
            void * t93 = gm_l0;
            gm_t88rcucg1b7uk t94 = gm_l5;
            gm_t6j6smj7qoctu t95;
            gm_b16_8 t96 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t95 + 0, t96);
            gm_b16_8 t97 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t95 + 16, t97);
            gm_tabb710d9mq2e t98;
            gm_f_format_dv5f37i((void *)&t98, (void *)NULL, cx, NULL, &t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 8, gm_src2j6ljeja7ilp, 380, 43);
            gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&t98) + 8));
            gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
            gm_b16_8 t101 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t93, t94, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t101);
            goto gm_fdone;
            goto me80;
mn91:;
ma82:;
            uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t103 != ((uint8_t)71ULL)) goto mn102;
            {
                void * t105 = gm_l0;
                gm_tfeab279c16rr t106;
                gm_b16_8 t107 = gm_l9;
                GM_ST(gm_b16_8, (uint8_t *)&t106 + 0, t107);
                gm_b16_8 t108 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t106 + 16, t108);
                uint64_t t109 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t109 >= (uintptr_t)(t110)) gm_trap_index(t109, t110, gm_src2j6ljeja7ilp, 382, 57);
                gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t109 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t106 + 32, t111);
                gm_b0_1 t112 = gm_f_linef_linqala((void *)NULL, cx, NULL, t105, &t106);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t113 = gm_l0;
                gm_b16_8 t114 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t114);
                goto gm_fdone;
            }
            goto me80;
mn102:;
ma83:;
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t116 != ((uint8_t)64ULL)) goto mn115;
            void * t117 = gm_l0;
            gm_t88rcucg1b7uk t118 = gm_l5;
            gm_t6j6smj7qoctu t119;
            gm_b16_8 t120 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t119 + 0, t120);
            uint64_t t121 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t122 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
            if (t121 >= (uintptr_t)(t122)) gm_trap_index(t121, t122, gm_src2j6ljeja7ilp, 385, 82);
            gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t121 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t119 + 16, t123);
            gm_tabb710d9mq2e t124;
            gm_f_format_6cn78ik((void *)&t124, (void *)NULL, cx, NULL, &t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 8, gm_src2j6ljeja7ilp, 385, 41);
            gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 8));
            gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
            gm_b16_8 t127 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t117, t118, t126);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t127);
            goto gm_fdone;
            goto me80;
mn115:;
ma84:;
            uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t129 != ((uint8_t)65ULL)) goto mn128;
            {
                void * t131 = gm_l0;
                gm_tfeab279c16rr t132;
                gm_b16_8 t133 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t132 + 0, t133);
                uint64_t t134 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t135 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t134 >= (uintptr_t)(t135)) gm_trap_index(t134, t135, gm_src2j6ljeja7ilp, 387, 58);
                gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t134 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t132 + 16, t136);
                uint64_t t137 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t138 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                if (t137 >= (uintptr_t)(t138)) gm_trap_index(t137, t138, gm_src2j6ljeja7ilp, 387, 67);
                gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t137 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t132 + 32, t139);
                gm_b0_1 t140 = gm_f_linef_6t3ttmd((void *)NULL, cx, NULL, t131, &t132);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t141 = gm_l0;
                gm_b16_8 t142 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t141);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t142);
                goto gm_fdone;
            }
            goto me80;
mn128:;
ma85:;
            uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t144 != ((uint8_t)66ULL)) goto mn143;
            void * t145 = gm_l0;
            gm_t88rcucg1b7uk t146 = gm_l5;
            gm_tfeab279c16rr t147;
            gm_b16_8 t148 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t147 + 0, t148);
            uint64_t t149 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t150 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t149 >= (uintptr_t)(t150)) gm_trap_index(t149, t150, gm_src2j6ljeja7ilp, 390, 90);
            gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t149 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t147 + 16, t151);
            uint64_t t152 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t153 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
            if (t152 >= (uintptr_t)(t153)) gm_trap_index(t152, t153, gm_src2j6ljeja7ilp, 390, 99);
            gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t152 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t147 + 32, t154);
            gm_tabb710d9mq2e t155;
            gm_f_format_25p33s1((void *)&t155, (void *)NULL, cx, NULL, &t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t155))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t155) + 8, gm_src2j6ljeja7ilp, 390, 41);
            gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&t155) + 8));
            gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
            gm_b16_8 t158 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t145, t146, t157);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t158);
            goto gm_fdone;
            goto me80;
mn143:;
ma86:;
            uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t160 != ((uint8_t)67ULL)) goto mn159;
            {
                void * t162 = gm_l0;
                gm_t88rcucg1b7uk t163 = gm_l8;
                uint64_t t164 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t164 >= (uintptr_t)(t165)) gm_trap_index(t164, t165, gm_src2j6ljeja7ilp, 392, 33);
                gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t164 * 16));
                gm_b16_8 t167 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t162, t163, t166);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t167;
            }
            {
                void * t168 = gm_l0;
                void * t169 = gm_l0;
                void * t170 = GM_LD(void *, (((uint8_t *)t169) + 0));
                void * t171 = (void *)(((uint8_t *)t170) + 8);
                gm_t88rcucg1b7uk t172 = gm_f_bool_ty_kotvo8t((void *)NULL, cx, NULL, t171);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tud7oovfvnf43 t173;
                gm_b16_8 t174 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t173 + 0, t174);
                gm_b16_8 t175 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t173 + 16, t175);
                uint64_t t176 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t177 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t176 >= (uintptr_t)(t177)) gm_trap_index(t176, t177, gm_src2j6ljeja7ilp, 393, 119);
                gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t176 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t173 + 32, t178);
                uint64_t t179 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t180 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                if (t179 >= (uintptr_t)(t180)) gm_trap_index(t179, t180, gm_src2j6ljeja7ilp, 393, 128);
                gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t179 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t173 + 48, t181);
                uint64_t t182 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t183 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                if (t182 >= (uintptr_t)(t183)) gm_trap_index(t182, t183, gm_src2j6ljeja7ilp, 393, 143);
                gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t182 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t173 + 64, t184);
                gm_tabb710d9mq2e t185;
                gm_f_format_5rrmec0((void *)&t185, (void *)NULL, cx, NULL, &t173);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t185))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t185) + 8, gm_src2j6ljeja7ilp, 393, 45);
                gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&t185) + 8));
                gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&t186));
                gm_b16_8 t188 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t168, t172, t187);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t188;
            }
            {
                void * t189 = gm_l0;
                gm_t88rcucg1b7uk t190 = gm_l5;
                gm_b16_8 t191 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t189, t190);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t191;
            }
            {
                void * t192 = gm_l0;
                gm_t88rcucg1b7uk t193 = gm_l5;
                gm_b16_8 t194 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t192, t193);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t194;
            }
            {
                void * t195 = gm_l0;
                gm_t88rcucg1b7uk t196 = gm_l5;
                gm_b16_8 t197 = gm_l17;
                gm_b16_8 t198 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t195, t196, t197);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l21 = t198;
            }
            {
                void * t199 = gm_l0;
                gm_t0uuhaljsf410 t200;
                gm_b16_8 t201 = gm_l19;
                GM_ST(gm_b16_8, (uint8_t *)&t200 + 0, t201);
                gm_b16_8 t202 = gm_l18;
                GM_ST(gm_b16_8, (uint8_t *)&t200 + 16, t202);
                gm_b16_8 t203 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t200 + 32, t203);
                gm_b16_8 t204 = gm_l21;
                GM_ST(gm_b16_8, (uint8_t *)&t200 + 48, t204);
                gm_b0_1 t205 = gm_f_linef_6h7r936((void *)NULL, cx, NULL, t199, &t200);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t206 = gm_l19;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t206);
                goto gm_fdone;
            }
            goto me80;
mn159:;
ma87:;
            uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t208 != ((uint8_t)68ULL)) goto mn207;
            gm_t9qkh0rsooc8d t209 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
            gm_l22 = t209;
            {
                gm_th2lqbo77pvsc t211;
                uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                switch (t220) { case ((uint8_t)0ULL): goto ma213; case ((uint8_t)1ULL): goto ma214; case ((uint8_t)2ULL): goto ma215; case ((uint8_t)3ULL): goto ma216; case ((uint8_t)4ULL): goto ma217; case ((uint8_t)5ULL): goto ma218; case ((uint8_t)6ULL): goto ma218; default: goto ma219; }
ma213:;
                uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                if (t222 != ((uint8_t)0ULL)) goto mn221;
                gm_b16_8 t223;
                GM_ST(void *, (uint8_t *)&t223, (void *)(gm_si0mnqg6j1eu8));
                GM_ST(uintptr_t, (uint8_t *)&t223 + 8, (uintptr_t)(18));
                gm_th2lqbo77pvsc t224;
                GM_ST(uint8_t, (uint8_t *)&t224 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t224 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t224 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t224), t223);
                ((uint8_t *)&t224)[16] = 1;
                t211 = t224;
                goto me212;
mn221:;
ma214:;
                uint8_t t226 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                if (t226 != ((uint8_t)1ULL)) goto mn225;
                gm_b16_8 t227;
                GM_ST(void *, (uint8_t *)&t227, (void *)(gm_sdeu8nuc597e8));
                GM_ST(uintptr_t, (uint8_t *)&t227 + 8, (uintptr_t)(18));
                gm_th2lqbo77pvsc t228;
                GM_ST(uint8_t, (uint8_t *)&t228 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t228 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t228 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t228), t227);
                ((uint8_t *)&t228)[16] = 1;
                t211 = t228;
                goto me212;
mn225:;
ma215:;
                uint8_t t230 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                if (t230 != ((uint8_t)2ULL)) goto mn229;
                gm_b16_8 t231;
                GM_ST(void *, (uint8_t *)&t231, (void *)(gm_s42h828lb45uf));
                GM_ST(uintptr_t, (uint8_t *)&t231 + 8, (uintptr_t)(18));
                gm_th2lqbo77pvsc t232;
                GM_ST(uint8_t, (uint8_t *)&t232 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t232 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t232 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t232), t231);
                ((uint8_t *)&t232)[16] = 1;
                t211 = t232;
                goto me212;
mn229:;
ma216:;
                uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                if (t234 != ((uint8_t)3ULL)) goto mn233;
                gm_b16_8 t235;
                GM_ST(void *, (uint8_t *)&t235, (void *)(gm_sm0f8bs4gd2ma));
                GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(17));
                gm_th2lqbo77pvsc t236;
                GM_ST(uint8_t, (uint8_t *)&t236 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t236 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t236 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t236), t235);
                ((uint8_t *)&t236)[16] = 1;
                t211 = t236;
                goto me212;
mn233:;
ma217:;
                uint8_t t238 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                if (t238 != ((uint8_t)4ULL)) goto mn237;
                gm_b16_8 t239;
                GM_ST(void *, (uint8_t *)&t239, (void *)(gm_sg3ba4vpic0jb));
                GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(18));
                gm_th2lqbo77pvsc t240;
                GM_ST(uint8_t, (uint8_t *)&t240 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t240 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t240 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t240), t239);
                ((uint8_t *)&t240)[16] = 1;
                t211 = t240;
                goto me212;
mn237:;
ma218:;
                uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                if (t244 != ((uint8_t)5ULL)) goto pa243;
                goto po242;
pa243:;
                uint8_t t246 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                if (t246 != ((uint8_t)6ULL)) goto pa245;
                goto po242;
pa245:;
                goto mn241;
po242:;
                gm_th2lqbo77pvsc t247;
                memset(&t247, 0, sizeof t247);
                t211 = t247;
                goto me212;
mn241:;
ma219:;
                gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 401, 26);
me212:;
                gm_l23 = t211;
            }
            {
                if (!((((uint8_t *)&gm_l23))[16] != 0)) goto ie248;
                gm_b16_8 t249 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                gm_l24 = t249;
                {
                    void * t251 = gm_l0;
                    gm_t88rcucg1b7uk t252 = gm_l5;
                    gm_t0uuhaljsf410 t253;
                    gm_b16_8 t254 = gm_l24;
                    GM_ST(gm_b16_8, (uint8_t *)&t253 + 0, t254);
                    gm_b16_8 t255 = gm_l10;
                    GM_ST(gm_b16_8, (uint8_t *)&t253 + 16, t255);
                    uint64_t t256 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t257 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    if (t256 >= (uintptr_t)(t257)) gm_trap_index(t256, t257, gm_src2j6ljeja7ilp, 410, 73);
                    gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t256 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t253 + 32, t258);
                    uint64_t t259 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t260 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                    if (t259 >= (uintptr_t)(t260)) gm_trap_index(t259, t260, gm_src2j6ljeja7ilp, 410, 82);
                    gm_b16_8 t261 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t259 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t253 + 48, t261);
                    gm_tabb710d9mq2e t262;
                    gm_f_format_patlqq0((void *)&t262, (void *)NULL, cx, NULL, &t253);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t262))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t262) + 8, gm_src2j6ljeja7ilp, 410, 34);
                    gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&t262) + 8));
                    gm_b16_8 t264 = GM_LD(gm_b16_8, ((uint8_t *)&t263));
                    gm_b16_8 t265 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t251, t252, t264);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t265);
                    goto gm_fdone;
                }
                goto ix248;
ie248:;
ix248:;
            }
            {
                gm_b16_8 t266;
                uint8_t t268 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l22) + 0) == ((uint8_t)5ULL)));
                if (!t268) goto ie267;
                gm_b16_8 t270;
                GM_ST(void *, (uint8_t *)&t270, (void *)(gm_sm5be4659ulci));
                GM_ST(uintptr_t, (uint8_t *)&t270 + 8, (uintptr_t)(1));
                gm_b16_8 t271 = t270;
                t266 = t271;
                goto ix267;
ie267:;
                gm_b16_8 t273;
                GM_ST(void *, (uint8_t *)&t273, (void *)(gm_s1f440st4mnkp));
                GM_ST(uintptr_t, (uint8_t *)&t273 + 8, (uintptr_t)(1));
                gm_b16_8 t274 = t273;
                t266 = t274;
ix267:;
                gm_l25 = t266;
            }
            {
                void * t275 = gm_l0;
                gm_t88rcucg1b7uk t276 = gm_l8;
                gm_b16_8 t277 = gm_l10;
                gm_tnpifv20irnph t278;
                GM_ST(gm_b16_8, ((uint8_t *)&t278 + 0), t277);
                gm_tabb710d9mq2e t279;
                gm_f_format_a1ch927((void *)&t279, (void *)NULL, cx, NULL, t278);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t279))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t279) + 8, gm_src2j6ljeja7ilp, 413, 33);
                gm_b16_8 t280 = GM_LD(gm_b16_8, (((uint8_t *)&t279) + 8));
                gm_b16_8 t281 = GM_LD(gm_b16_8, ((uint8_t *)&t280));
                gm_b16_8 t282 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t275, t276, t281);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l26 = t282;
            }
            {
                uint64_t t283 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t284 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t283 >= (uintptr_t)(t284)) gm_trap_index(t283, t284, gm_src2j6ljeja7ilp, 414, 18);
                gm_b16_8 t285 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t283 * 16));
                gm_l27 = t285;
            }
            {
                void * t286 = gm_l0;
                gm_tg1t0c5hkovn5 t287;
                gm_b16_8 t288 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t287 + 0, t288);
                gm_b16_8 t289 = gm_l26;
                GM_ST(gm_b16_8, (uint8_t *)&t287 + 16, t289);
                gm_b16_8 t290 = gm_l27;
                GM_ST(gm_b16_8, (uint8_t *)&t287 + 32, t290);
                gm_b16_8 t291 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t287 + 48, t291);
                gm_b16_8 t292 = gm_l26;
                GM_ST(gm_b16_8, (uint8_t *)&t287 + 64, t292);
                gm_b16_8 t293 = gm_l27;
                GM_ST(gm_b16_8, (uint8_t *)&t287 + 80, t293);
                gm_b16_8 t294 = gm_l26;
                GM_ST(gm_b16_8, (uint8_t *)&t287 + 96, t294);
                uint64_t t295 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t296 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                if (t295 >= (uintptr_t)(t296)) gm_trap_index(t295, t296, gm_src2j6ljeja7ilp, 415, 150);
                gm_b16_8 t297 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t295 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t287 + 112, t297);
                gm_b0_1 t298 = gm_f_linef_n685kk5((void *)NULL, cx, NULL, t286, &t287);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t299 = gm_l26;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t299);
                goto gm_fdone;
            }
            goto me80;
mn207:;
ma88:;
            gm_trap("reached `@unreachable()`", gm_src2j6ljeja7ilp, 418, 14);
            goto me80;
mn300:;
ma89:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 379, 5);
me80:;
        }
        goto ix10;
ie10:;
        {
            void * t302 = gm_l1;
            gm_tl874t2atnlh3 t303 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t302) + 72));
            gm_b16_8 t304;
            GM_ST(void *, (uint8_t *)&t304, (void *)(gm_sn8alsh9na9ne));
            GM_ST(uintptr_t, (uint8_t *)&t304 + 8, (uintptr_t)(11));
            gm_t08olq97rscn3 t305;
            gm_f_unsupported_531q51r((void *)&t305, (void *)NULL, cx, NULL, t303, t304);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t305);
            goto gm_fdone;
        }
ix10:;
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
__attribute__((aligned(4))) void gm_f_bit_count_77uni44(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_bit_count_77uni44 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* at */
    gm_b16_8 gm_l4; /* v0 */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_tuglhjjmj2g58 gm_l6; /* ii */
    gm_b16_8 gm_l7; /* x0 */
    gm_b16_8 gm_l8; /* x */
    gm_b16_8 gm_l9; /* c */
    gm_b16_8 gm_l10; /* call */
    gm_b16_8 gm_l11; /* r */
    gm_tuglhjjmj2g58 gm_l12; /* ri */
    gm_b16_8 gm_l13; /* n */
    GM_STACK_CHECK(&F, 1565u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_t88rcucg1b7uk t5 = gm_l3;
        gm_tqcc6dmlne11b t6;
        gm_f_int_info_2tcdf1n((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t6))[16] != 0)) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 314, 11);
        gm_tuglhjjmj2g58 t7 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t6));
        gm_l6 = t7;
    }
    {
        gm_t6j6smj7qoctu t8;
        gm_tuglhjjmj2g58 t9 = gm_l6;
        gm_b16_8 t10 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t9));
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 0, t10);
        gm_b16_8 t11 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 16, t11);
        gm_tabb710d9mq2e t12;
        gm_f_format_21vnq4a((void *)&t12, (void *)NULL, cx, NULL, &t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 8, gm_src2j6ljeja7ilp, 315, 11);
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
        gm_l7 = t13;
    }
    {
        gm_b16_8 t14;
        uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        uint8_t t17 = (t16 >= ((uint32_t)128ULL));
        if (!t17) goto ie15;
        gm_b16_8 t19 = gm_l7;
        gm_b16_8 t20 = t19;
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        t14 = t21;
        goto ix15;
ie15:;
        gm_tommf4j9upomc t23;
        memset((uint8_t *)&t23 + 20, 0, 4);
        gm_b16_8 t24 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t23 + 0, t24);
        uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        GM_ST(uint32_t, (uint8_t *)&t23 + 16, t25);
        gm_tabb710d9mq2e t26;
        gm_f_format_s2ml2tu((void *)&t26, (void *)NULL, cx, NULL, &t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_src2j6ljeja7ilp, 317, 49);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
        gm_b16_8 t28 = t27;
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        t14 = t29;
ix15:;
        gm_l8 = t14;
    }
    {
        void * t30 = gm_l0;
        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
        void * t32 = (void *)(((uint8_t *)t31) + 8);
        gm_t88rcucg1b7uk t33 = gm_l5;
        gm_b16_8 t34 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t34;
    }
    {
        gm_b16_8 t35;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t43) { case ((uint8_t)0ULL): goto ma41; case ((uint8_t)1ULL): goto ma41; case ((uint8_t)2ULL): goto ma41; case ((uint8_t)3ULL): goto ma41; case ((uint8_t)4ULL): goto ma41; case ((uint8_t)5ULL): goto ma41; case ((uint8_t)6ULL): goto ma41; case ((uint8_t)7ULL): goto ma41; case ((uint8_t)8ULL): goto ma41; case ((uint8_t)9ULL): goto ma41; case ((uint8_t)10ULL): goto ma41; case ((uint8_t)11ULL): goto ma41; case ((uint8_t)12ULL): goto ma41; case ((uint8_t)13ULL): goto ma41; case ((uint8_t)14ULL): goto ma41; case ((uint8_t)15ULL): goto ma41; case ((uint8_t)16ULL): goto ma41; case ((uint8_t)17ULL): goto ma41; case ((uint8_t)18ULL): goto ma41; case ((uint8_t)19ULL): goto ma41; case ((uint8_t)20ULL): goto ma41; case ((uint8_t)21ULL): goto ma41; case ((uint8_t)22ULL): goto ma41; case ((uint8_t)23ULL): goto ma41; case ((uint8_t)24ULL): goto ma41; case ((uint8_t)25ULL): goto ma41; case ((uint8_t)26ULL): goto ma41; case ((uint8_t)27ULL): goto ma41; case ((uint8_t)28ULL): goto ma37; case ((uint8_t)29ULL): goto ma38; case ((uint8_t)30ULL): goto ma39; case ((uint8_t)31ULL): goto ma40; case ((uint8_t)32ULL): goto ma41; case ((uint8_t)33ULL): goto ma41; case ((uint8_t)34ULL): goto ma41; case ((uint8_t)35ULL): goto ma41; case ((uint8_t)36ULL): goto ma41; case ((uint8_t)37ULL): goto ma41; case ((uint8_t)38ULL): goto ma41; case ((uint8_t)39ULL): goto ma41; case ((uint8_t)40ULL): goto ma41; case ((uint8_t)41ULL): goto ma41; case ((uint8_t)42ULL): goto ma41; case ((uint8_t)43ULL): goto ma41; case ((uint8_t)44ULL): goto ma41; case ((uint8_t)45ULL): goto ma41; case ((uint8_t)46ULL): goto ma41; case ((uint8_t)47ULL): goto ma41; case ((uint8_t)48ULL): goto ma41; case ((uint8_t)49ULL): goto ma41; case ((uint8_t)50ULL): goto ma41; case ((uint8_t)51ULL): goto ma41; case ((uint8_t)52ULL): goto ma41; case ((uint8_t)53ULL): goto ma41; case ((uint8_t)54ULL): goto ma41; case ((uint8_t)55ULL): goto ma41; case ((uint8_t)56ULL): goto ma41; case ((uint8_t)57ULL): goto ma41; case ((uint8_t)58ULL): goto ma41; case ((uint8_t)59ULL): goto ma41; case ((uint8_t)60ULL): goto ma41; case ((uint8_t)61ULL): goto ma41; case ((uint8_t)62ULL): goto ma41; case ((uint8_t)63ULL): goto ma41; case ((uint8_t)64ULL): goto ma41; case ((uint8_t)65ULL): goto ma41; case ((uint8_t)66ULL): goto ma41; case ((uint8_t)67ULL): goto ma41; case ((uint8_t)68ULL): goto ma41; case ((uint8_t)69ULL): goto ma41; case ((uint8_t)70ULL): goto ma41; case ((uint8_t)71ULL): goto ma41; case ((uint8_t)72ULL): goto ma41; case ((uint8_t)73ULL): goto ma41; case ((uint8_t)74ULL): goto ma41; case ((uint8_t)75ULL): goto ma41; case ((uint8_t)76ULL): goto ma41; case ((uint8_t)77ULL): goto ma41; case ((uint8_t)78ULL): goto ma41; case ((uint8_t)79ULL): goto ma41; case ((uint8_t)80ULL): goto ma41; case ((uint8_t)81ULL): goto ma41; default: goto ma41; }
ma37:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t45 != ((uint8_t)28ULL)) goto mn44;
        gm_tommf4j9upomc t46;
        memset((uint8_t *)&t46 + 20, 0, 4);
        gm_b16_8 t47 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 0, t47);
        uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        GM_ST(uint32_t, (uint8_t *)&t46 + 16, t48);
        gm_tabb710d9mq2e t49;
        gm_f_format_c9b4vl3((void *)&t49, (void *)NULL, cx, NULL, &t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 8, gm_src2j6ljeja7ilp, 320, 17);
        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 8));
        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
        t35 = t51;
        goto me36;
mn44:;
ma38:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t53 != ((uint8_t)29ULL)) goto mn52;
        gm_tommf4j9upomc t54;
        memset((uint8_t *)&t54 + 20, 0, 4);
        gm_b16_8 t55 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t54 + 0, t55);
        uint32_t t56 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        GM_ST(uint32_t, (uint8_t *)&t54 + 16, t56);
        gm_tabb710d9mq2e t57;
        gm_f_format_a1ec0ji((void *)&t57, (void *)NULL, cx, NULL, &t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 8, gm_src2j6ljeja7ilp, 321, 17);
        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t57) + 8));
        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
        t35 = t59;
        goto me36;
mn52:;
ma39:;
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t61 != ((uint8_t)30ULL)) goto mn60;
        gm_b16_8 t62 = gm_l8;
        gm_tnpifv20irnph t63;
        GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t62);
        gm_tabb710d9mq2e t64;
        gm_f_format_q296tqt((void *)&t64, (void *)NULL, cx, NULL, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 8, gm_src2j6ljeja7ilp, 322, 22);
        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
        t35 = t66;
        goto me36;
mn60:;
ma40:;
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t68 != ((uint8_t)31ULL)) goto mn67;
        {
            uint32_t t71 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
            if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_src2j6ljeja7ilp, 324, 16);
            uint32_t t72 = (uint32_t)(t71 % ((uint32_t)8ULL));
            uint8_t t73 = (!(t72 == ((uint32_t)0ULL)));
            if (!t73) goto ie70;
            {
                void * t75 = gm_l1;
                gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t75) + 72));
                gm_b16_8 t77;
                GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s0cpkv7s4ef85));
                GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(23));
                gm_t08olq97rscn3 t78;
                gm_f_unsupported_531q51r((void *)&t78, (void *)NULL, cx, NULL, t76, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t78);
                goto gm_fdone;
            }
            goto ix70;
ie70:;
ix70:;
        }
        gm_tommf4j9upomc t79;
        memset((uint8_t *)&t79 + 20, 0, 4);
        gm_b16_8 t80 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t79 + 0, t80);
        uint32_t t81 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        GM_ST(uint32_t, (uint8_t *)&t79 + 16, t81);
        gm_tabb710d9mq2e t82;
        gm_f_format_87dn9rn((void *)&t82, (void *)NULL, cx, NULL, &t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 8, gm_src2j6ljeja7ilp, 325, 13);
        gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
        gm_b16_8 t84 = t83;
        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
        t35 = t85;
        goto me36;
mn67:;
ma41:;
        gm_tommf4j9upomc t87;
        memset((uint8_t *)&t87 + 20, 0, 4);
        gm_b16_8 t88 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t87 + 0, t88);
        uint32_t t89 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        GM_ST(uint32_t, (uint8_t *)&t87 + 16, t89);
        gm_tabb710d9mq2e t90;
        gm_f_format_kbs7h9q((void *)&t90, (void *)NULL, cx, NULL, &t87);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 8, gm_src2j6ljeja7ilp, 327, 14);
        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&t90) + 8));
        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
        t35 = t92;
        goto me36;
mn86:;
ma42:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 319, 20);
me36:;
        gm_l10 = t35;
    }
    {
        void * t93 = gm_l0;
        gm_t88rcucg1b7uk t94 = gm_l5;
        gm_t6j6smj7qoctu t95;
        gm_b16_8 t96 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t95 + 0, t96);
        gm_b16_8 t97 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t95 + 16, t97);
        gm_tabb710d9mq2e t98;
        gm_f_format_3vtl3te((void *)&t98, (void *)NULL, cx, NULL, &t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 8, gm_src2j6ljeja7ilp, 329, 20);
        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&t98) + 8));
        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
        gm_b16_8 t101 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t93, t94, t100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t101;
    }
    {
        gm_tvdg94bk7aa22 t103 = gm_l2;
        gm_tq9t4giin3pm0 t104;
        gm_tvdg94bk7aa22 t105;
        GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t105 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)31ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t104 + 0, t105);
        gm_tvdg94bk7aa22 t106;
        GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t106 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)34ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t104 + 12, t106);
        uint8_t t107 = 0;
        if (gm_eqg63o53ar52(((uint8_t *)&t104) + 0, ((uint8_t *)&t103))) t107 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t104) + 12, ((uint8_t *)&t103))) t107 = 1;
        uint8_t t108 = (uint8_t)(t107);
        if (!t108) goto ie102;
        {
            void * t111 = gm_l0;
            gm_t88rcucg1b7uk t112 = gm_l5;
            gm_tqcc6dmlne11b t113;
            gm_f_int_info_2tcdf1n((void *)&t113, (void *)NULL, cx, NULL, t111, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t113))[16] != 0)) goto ie110;
            gm_tuglhjjmj2g58 t114 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t113));
            gm_l12 = t114;
            {
                gm_tuglhjjmj2g58 t116 = gm_l12;
                gm_b16_8 t117 = gm_l11;
                gm_b16_8 t118 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t116, t117);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t118;
            }
            {
                void * t119 = gm_l0;
                gm_t88rcucg1b7uk t120 = gm_l5;
                gm_b16_8 t121 = gm_l13;
                gm_b16_8 t122 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t119, t120, t121);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t122);
                goto gm_fdone;
            }
            goto ix110;
ie110:;
ix110:;
        }
        goto ix102;
ie102:;
ix102:;
    }
    {
        gm_b16_8 t123 = gm_l11;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t123);
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
__attribute__((aligned(4))) void gm_f_black_box_r6deojc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_black_box_r6deojc F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* v0 */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* c */
    uint64_t gm_l5; /* storage */
    uint8_t gm_l6; /* in_register */
    gm_b16_8 gm_l7; /* v */
    gm_b16_8 gm_l8; /* slot */
    gm_b16_8 gm_l9; /* p */
    GM_STACK_CHECK(&F, 691u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        void * t7 = (void *)(((uint8_t *)t6) + 8);
        gm_t88rcucg1b7uk t8 = gm_l3;
        uint8_t t9 = gm_f_is_unit_17uennd(t7, GM_CONV(gm_csguankko4lmhd, t8));
        if (!t9) goto ie4;
        {
            gm_b16_8 t11 = gm_l2;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t11);
            goto gm_fdone;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t12 = gm_l0;
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
        void * t14 = (void *)(((uint8_t *)t13) + 8);
        gm_t88rcucg1b7uk t15 = gm_l3;
        gm_b16_8 t16 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t16;
    }
    {
        uint8_t t17 = 0;
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
        uint8_t t20 = GM_LD(uint8_t, ((((((uint8_t *)t19) + 8) + 8) + 0) + 56));
        uint8_t t21 = (uint8_t)!t20;
        if (t21) {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = (void *)(((uint8_t *)t23) + 8);
            gm_t88rcucg1b7uk t25 = gm_l3;
            gm_t5v8n6qrn4rh4 t26;
            gm_f_repr_53quqki((void *)&t26, (void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t27;
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
            switch (t33) { case ((uint8_t)0ULL): goto ma29; case ((uint8_t)1ULL): goto ma31; case ((uint8_t)2ULL): goto ma30; case ((uint8_t)3ULL): goto ma31; case ((uint8_t)4ULL): goto ma31; case ((uint8_t)5ULL): goto ma31; case ((uint8_t)6ULL): goto ma31; default: goto ma31; }
ma29:;
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
            if (t35 != ((uint8_t)0ULL)) goto mn34;
            uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)&t26) + 8) + 8));
            gm_l5 = t36;
            uint64_t t37 = gm_l5;
            uint8_t t38 = (t37 <= ((uint64_t)8ULL));
            t27 = t38;
            goto me28;
mn34:;
ma30:;
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
            if (t40 != ((uint8_t)2ULL)) goto mn39;
            t27 = ((uint8_t)1);
            goto me28;
mn39:;
ma31:;
            t27 = ((uint8_t)0);
            goto me28;
mn41:;
ma32:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 431, 57);
me28:;
            t17 = t27;
        } else t17 = 0;
        gm_l6 = t17;
    }
    {
        uint8_t t43 = gm_l6;
        if (!t43) goto ie42;
        {
            void * t45 = gm_l0;
            gm_t88rcucg1b7uk t46 = gm_l3;
            gm_b16_8 t47 = gm_l2;
            gm_b16_8 t48 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t45, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t48;
        }
        {
            void * t49 = gm_l0;
            gm_b16_8 t50 = gm_l7;
            gm_tnpifv20irnph t51;
            GM_ST(gm_b16_8, ((uint8_t *)&t51 + 0), t50);
            gm_b0_1 t52 = gm_f_linef_7e19lva((void *)NULL, cx, NULL, t49, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t53 = gm_l7;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t53);
            goto gm_fdone;
        }
        goto ix42;
ie42:;
ix42:;
    }
    {
        void * t54 = gm_l0;
        gm_t88rcucg1b7uk t55 = gm_l3;
        gm_b16_8 t56 = gm_l2;
        gm_b16_8 t57 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t54, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t57;
    }
    {
        void * t58 = gm_l0;
        gm_b16_8 t59 = gm_f_fresh_tmp_6tadl69((void *)NULL, cx, NULL, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t59;
    }
    {
        void * t60 = gm_l0;
        gm_tfeab279c16rr t61;
        gm_b16_8 t62 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t61 + 0, t62);
        gm_b16_8 t63 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t61 + 16, t63);
        gm_b16_8 t64 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t61 + 32, t64);
        gm_b0_1 t65 = gm_f_linef_0kkt9cm((void *)NULL, cx, NULL, t60, &t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t66 = gm_l0;
        gm_t88rcucg1b7uk t67 = gm_l3;
        gm_b16_8 t68 = gm_l9;
        gm_tnpifv20irnph t69;
        GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t68);
        gm_tabb710d9mq2e t70;
        gm_f_format_ohk11b5((void *)&t70, (void *)NULL, cx, NULL, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 8, gm_src2j6ljeja7ilp, 444, 22);
        gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
        gm_b16_8 t73 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t66, t67, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t73);
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
__attribute__((aligned(4))) void gm_f_builtin_s7ark4p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3) {
    struct gm_fr_builtin_s7ark4p F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* r */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* loc */
    void * gm_l7; /* tys */
    gm_tcc1rq4n1avb2 gm_l8; /* counts */
    gm_t9qkh0rsooc8d gm_l9; /* sop */
    uint32_t gm_l10; /* id */
    gm_b16_8 gm_l11; /* s */
    gm_b16_8 gm_l12; /* a */
    gm_b16_8 gm_l13; /* p */
    gm_b16_8 gm_l14; /* p */
    gm_b16_8 gm_l15; /* s */
    gm_b16_8 gm_l16; /* a */
    gm_t88rcucg1b7uk gm_l17; /* elem */
    gm_b16_8 gm_l18; /* d */
    gm_b16_8 gm_l19; /* s */
    gm_b16_8 gm_l20; /* n */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    gm_b16_8 gm_l25; /* v0 */
    gm_t88rcucg1b7uk gm_l26; /* at */
    gm_b16_8 gm_l27; /* a */
    gm_b16_8 gm_l28; /* r */
    uint64_t gm_l29; /* sz */
    gm_tuglhjjmj2g58 gm_l30; /* ii */
    gm_b16_8 gm_l31; /* nv */
    uint64_t gm_l32; /* al */
    gm_t88rcucg1b7uk gm_l33; /* elem */
    uint64_t gm_l34; /* stride */
    gm_b16_8 gm_l35; /* p */
    gm_b16_8 gm_l36; /* x */
    gm_b16_8 gm_l37; /* sign */
    gm_b16_8 gm_l38; /* a */
    gm_b16_8 gm_l39; /* gt */
    gm_b16_8 gm_l40; /* f */
    gm_b16_8 gm_l41; /* r */
    gm_b16_8 gm_l42; /* x */
    gm_b16_8 gm_l43; /* lo */
    gm_b16_8 gm_l44; /* hi */
    gm_b16_8 gm_l45; /* r */
    gm_tuglhjjmj2g58 gm_l46; /* ii */
    gm_b16_8 gm_l47; /* min */
    gm_tuglhjjmj2g58 gm_l48; /* ii */
    gm_b16_8 gm_l49; /* u */
    gm_b16_8 gm_l50; /* a */
    gm_b16_8 gm_l51; /* b */
    gm_b16_8 gm_l52; /* f */
    gm_b16_8 gm_l53; /* r */
    gm_b16_8 gm_l54; /* f */
    gm_b16_8 gm_l55; /* r */
    gm_t9qkh0rsooc8d gm_l56; /* fl */
    gm_b16_8 gm_l57; /* a */
    gm_b16_8 gm_l58; /* f */
    gm_b16_8 gm_l59; /* a */
    GM_STACK_CHECK(&F, 10634u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l1;
        gm_tvdg94bk7aa22 t5 = gm_l2;
        gm_b16_8 t6 = gm_l3;
        gm_t6j4d6ei14c2t t7;
        gm_f_vector_builtin_a1f1ati((void *)&t7, (void *)NULL, cx, NULL, t3, t4, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_t5o0tpv5jka2l t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t8 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t7) + 8)));
            F.ret = t8;
            goto gm_fret;
        }
        if (!(((((uint8_t *)&t7) + 8))[16] != 0)) goto ie2;
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_l4 = t9;
        {
            gm_b16_8 t11 = gm_l4;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t11);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l1;
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t12) + 0));
        gm_l5 = t13;
    }
    {
        void * t14 = gm_l0;
        void * t15 = gm_l1;
        gm_b16_8 t16 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t16;
    }
    {
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
        void * t19 = GM_LD(void *, ((((uint8_t *)t18) + 8) + 0));
        void * t20 = (void *)(((uint8_t *)t19) + 8);
        gm_l7 = t20;
    }
    {
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t35) { case ((uint8_t)0ULL): goto ma33; case ((uint8_t)1ULL): goto ma33; case ((uint8_t)2ULL): goto ma33; case ((uint8_t)3ULL): goto ma33; case ((uint8_t)4ULL): goto ma33; case ((uint8_t)5ULL): goto ma33; case ((uint8_t)6ULL): goto ma33; case ((uint8_t)7ULL): goto ma33; case ((uint8_t)8ULL): goto ma33; case ((uint8_t)9ULL): goto ma33; case ((uint8_t)10ULL): goto ma33; case ((uint8_t)11ULL): goto ma33; case ((uint8_t)12ULL): goto ma33; case ((uint8_t)13ULL): goto ma33; case ((uint8_t)14ULL): goto ma33; case ((uint8_t)15ULL): goto ma33; case ((uint8_t)16ULL): goto ma33; case ((uint8_t)17ULL): goto ma33; case ((uint8_t)18ULL): goto ma33; case ((uint8_t)19ULL): goto ma33; case ((uint8_t)20ULL): goto ma33; case ((uint8_t)21ULL): goto ma33; case ((uint8_t)22ULL): goto ma33; case ((uint8_t)23ULL): goto ma33; case ((uint8_t)24ULL): goto ma33; case ((uint8_t)25ULL): goto ma33; case ((uint8_t)26ULL): goto ma33; case ((uint8_t)27ULL): goto ma33; case ((uint8_t)28ULL): goto ma33; case ((uint8_t)29ULL): goto ma33; case ((uint8_t)30ULL): goto ma33; case ((uint8_t)31ULL): goto ma33; case ((uint8_t)32ULL): goto ma33; case ((uint8_t)33ULL): goto ma33; case ((uint8_t)34ULL): goto ma33; case ((uint8_t)35ULL): goto ma33; case ((uint8_t)36ULL): goto ma28; case ((uint8_t)37ULL): goto ma28; case ((uint8_t)38ULL): goto ma28; case ((uint8_t)39ULL): goto ma28; case ((uint8_t)40ULL): goto ma33; case ((uint8_t)41ULL): goto ma33; case ((uint8_t)42ULL): goto ma33; case ((uint8_t)43ULL): goto ma33; case ((uint8_t)44ULL): goto ma33; case ((uint8_t)45ULL): goto ma33; case ((uint8_t)46ULL): goto ma33; case ((uint8_t)47ULL): goto ma33; case ((uint8_t)48ULL): goto ma33; case ((uint8_t)49ULL): goto ma33; case ((uint8_t)50ULL): goto ma33; case ((uint8_t)51ULL): goto ma33; case ((uint8_t)52ULL): goto ma33; case ((uint8_t)53ULL): goto ma33; case ((uint8_t)54ULL): goto ma33; case ((uint8_t)55ULL): goto ma33; case ((uint8_t)56ULL): goto ma33; case ((uint8_t)57ULL): goto ma33; case ((uint8_t)58ULL): goto ma33; case ((uint8_t)59ULL): goto ma33; case ((uint8_t)60ULL): goto ma33; case ((uint8_t)61ULL): goto ma22; case ((uint8_t)62ULL): goto ma33; case ((uint8_t)63ULL): goto ma27; case ((uint8_t)64ULL): goto ma33; case ((uint8_t)65ULL): goto ma33; case ((uint8_t)66ULL): goto ma33; case ((uint8_t)67ULL): goto ma33; case ((uint8_t)68ULL): goto ma33; case ((uint8_t)69ULL): goto ma33; case ((uint8_t)70ULL): goto ma33; case ((uint8_t)71ULL): goto ma33; case ((uint8_t)72ULL): goto ma25; case ((uint8_t)73ULL): goto ma23; case ((uint8_t)74ULL): goto ma29; case ((uint8_t)75ULL): goto ma31; case ((uint8_t)76ULL): goto ma30; case ((uint8_t)77ULL): goto ma30; case ((uint8_t)78ULL): goto ma32; case ((uint8_t)79ULL): goto ma24; case ((uint8_t)80ULL): goto ma33; case ((uint8_t)81ULL): goto ma26; default: goto ma33; }
ma22:;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t37 != ((uint8_t)61ULL)) goto mn36;
        void * t38 = gm_l0;
        void * t39 = gm_l1;
        gm_b16_8 t40 = gm_l3;
        gm_f_assert_m8ussus((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t38, t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me21;
mn36:;
ma23:;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t42 != ((uint8_t)73ULL)) goto mn41;
        void * t43 = gm_l0;
        void * t44 = gm_l1;
        gm_b16_8 t45 = gm_l3;
        gm_t5o0tpv5jka2l t46;
        gm_f_fiber_builtin_bgfcp56((void *)&t46, (void *)NULL, cx, NULL, t43, t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t46))[0] != 0)) {
            gm_t5o0tpv5jka2l t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
            ((uint8_t *)&t47)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t47 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t46) + 8)));
            F.ret = t47;
            goto gm_fret;
        }
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t48);
        goto gm_fdone;
        goto me21;
mn41:;
ma24:;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t50 != ((uint8_t)79ULL)) goto mn49;
        gm_tcc1rq4n1avb2 t51 = GM_LD(gm_tcc1rq4n1avb2, (((uint8_t *)&gm_l2) + 4));
        gm_l8 = t51;
        void * t52 = gm_l0;
        gm_b16_8 t53 = gm_l3;
        uint32_t t54 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
        uint64_t t55 = (uint64_t)t54;
        uint32_t t56 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 4));
        uint64_t t57 = (uint64_t)t56;
        gm_t5o0tpv5jka2l t58;
        gm_f_arith_asm_pklrmbh((void *)&t58, (void *)NULL, cx, NULL, t52, t53, t55, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t58))[0] != 0)) {
            gm_t5o0tpv5jka2l t59;
            GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
            ((uint8_t *)&t59)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t59 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t58) + 8)));
            F.ret = t59;
            goto gm_fret;
        }
        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t60);
        goto gm_fdone;
        goto me21;
mn49:;
ma25:;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t62 != ((uint8_t)72ULL)) goto mn61;
        gm_t9qkh0rsooc8d t63 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
        gm_l9 = t63;
        void * t64 = gm_l0;
        void * t65 = gm_l1;
        gm_t9qkh0rsooc8d t66 = gm_l9;
        gm_b16_8 t67 = gm_l3;
        gm_f_sys_builtin_svdoho3((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t64, t65, t66, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me21;
mn61:;
ma26:;
        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t69 != ((uint8_t)81ULL)) goto mn68;
        uint32_t t70 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        gm_l10 = t70;
        {
            void * t72 = gm_l0;
            uint32_t t73 = gm_l10;
            gm_t88rcucg1b7uk t74;
            GM_ST(uint32_t, ((uint8_t *)&t74 + 0), t73);
            gm_b0_1 t75 = gm_f_linef_10398ks((void *)NULL, cx, NULL, t72, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t76 = gm_l0;
            gm_b16_8 t77 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t77);
            goto gm_fdone;
        }
        goto me21;
mn68:;
ma27:;
        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t79 != ((uint8_t)63ULL)) goto mn78;
        {
            void * t81 = gm_l0;
            gm_b16_8 t82 = gm_l6;
            gm_tnpifv20irnph t83;
            GM_ST(gm_b16_8, ((uint8_t *)&t83 + 0), t82);
            gm_b0_1 t84 = gm_f_linef_oi4f7u8((void *)NULL, cx, NULL, t81, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t85 = gm_l0;
            gm_t88rcucg1b7uk t86 = gm_l5;
            gm_b16_8 t87 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t87);
            goto gm_fdone;
        }
        goto me21;
mn78:;
ma28:;
        uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t91 != ((uint8_t)36ULL)) goto pa90;
        goto po89;
pa90:;
        uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t93 != ((uint8_t)37ULL)) goto pa92;
        goto po89;
pa92:;
        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t95 != ((uint8_t)38ULL)) goto pa94;
        goto po89;
pa94:;
        uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t97 != ((uint8_t)39ULL)) goto pa96;
        goto po89;
pa96:;
        goto mn88;
po89:;
        void * t98 = gm_l0;
        void * t99 = gm_l1;
        gm_tvdg94bk7aa22 t100 = gm_l2;
        gm_b16_8 t101 = gm_l3;
        gm_t5o0tpv5jka2l t102;
        gm_f_arith_get_jao7f8p((void *)&t102, (void *)NULL, cx, NULL, t98, t99, t100, t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t102))[0] != 0)) {
            gm_t5o0tpv5jka2l t103;
            GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
            ((uint8_t *)&t103)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t103 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t102) + 8)));
            F.ret = t103;
            goto gm_fret;
        }
        gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&t102) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t104);
        goto gm_fdone;
        goto me21;
mn88:;
ma29:;
        uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t106 != ((uint8_t)74ULL)) goto mn105;
        {
            void * t108 = gm_l0;
            uint64_t t109 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t109 >= (uintptr_t)(t110)) gm_trap_index(t109, t110, gm_src2j6ljeja7ilp, 56, 31);
            void * t111 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t109 * 88);
            gm_t5o0tpv5jka2l t112;
            gm_f_expr_porbc9t((void *)&t112, (void *)NULL, cx, NULL, t108, t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t112))[0] != 0)) {
                gm_t5o0tpv5jka2l t113;
                GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
                ((uint8_t *)&t113)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t113 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t112) + 8)));
                F.ret = t113;
                goto gm_fret;
            }
            gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&t112) + 8));
            gm_l11 = t114;
        }
        {
            void * t115 = gm_l0;
            uint64_t t116 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t116 >= (uintptr_t)(t117)) gm_trap_index(t116, t117, gm_src2j6ljeja7ilp, 57, 31);
            void * t118 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t116 * 88);
            gm_t5o0tpv5jka2l t119;
            gm_f_expr_porbc9t((void *)&t119, (void *)NULL, cx, NULL, t115, t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t119))[0] != 0)) {
                gm_t5o0tpv5jka2l t120;
                GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
                ((uint8_t *)&t120)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t120 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t119) + 8)));
                F.ret = t120;
                goto gm_fret;
            }
            gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&t119) + 8));
            gm_l12 = t121;
        }
        {
            void * t122 = gm_l0;
            gm_t0uuhaljsf410 t123;
            gm_b16_8 t124 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t123 + 0, t124);
            gm_b16_8 t125 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t123 + 16, t125);
            gm_b16_8 t126 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t123 + 32, t126);
            gm_b16_8 t127 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t123 + 48, t127);
            gm_b0_1 t128 = gm_f_linef_vh7uebp((void *)NULL, cx, NULL, t122, &t123);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t129 = gm_l0;
            void * t130 = gm_l7;
            gm_t53at80pfdus3 t131;
            memset(&t131, 0, sizeof t131);
            GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)7ULL));
            gm_t88rcucg1b7uk t132 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t130, &t131);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t133;
            gm_b16_8 t134 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t133 + 0, t134);
            gm_b16_8 t135 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t133 + 16, t135);
            gm_tabb710d9mq2e t136;
            gm_f_format_7g5nhjp((void *)&t136, (void *)NULL, cx, NULL, &t133);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t136))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t136) + 8, gm_src2j6ljeja7ilp, 59, 46);
            gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&t136) + 8));
            gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
            gm_b16_8 t139 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t129, t132, t138);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t139;
        }
        {
            void * t140 = gm_l0;
            gm_t88rcucg1b7uk t141 = gm_l5;
            gm_b16_8 t142 = gm_l13;
            gm_b16_8 t143 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t140, t141, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t143);
            goto gm_fdone;
        }
        goto me21;
mn105:;
ma30:;
        uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t147 != ((uint8_t)76ULL)) goto pa146;
        goto po145;
pa146:;
        uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t149 != ((uint8_t)77ULL)) goto pa148;
        goto po145;
pa148:;
        goto mn144;
po145:;
        void * t150 = gm_l0;
        void * t151 = gm_l1;
        gm_tvdg94bk7aa22 t152 = gm_l2;
        gm_b16_8 t153 = gm_l3;
        gm_f_alloc_tag_a71p35s((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t150, t151, t152, t153);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me21;
mn144:;
ma31:;
        uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t155 != ((uint8_t)75ULL)) goto mn154;
        {
            void * t157 = gm_l0;
            uint64_t t158 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t158 >= (uintptr_t)(t159)) gm_trap_index(t158, t159, gm_src2j6ljeja7ilp, 64, 31);
            void * t160 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t158 * 88);
            gm_t5o0tpv5jka2l t161;
            gm_f_expr_porbc9t((void *)&t161, (void *)NULL, cx, NULL, t157, t160);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t161))[0] != 0)) {
                gm_t5o0tpv5jka2l t162;
                GM_ST(uint8_t, (uint8_t *)&t162 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t162 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
                ((uint8_t *)&t162)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t162 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t161) + 8)));
                F.ret = t162;
                goto gm_fret;
            }
            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&t161) + 8));
            gm_l14 = t163;
        }
        {
            void * t164 = gm_l0;
            uint64_t t165 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t166 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t165 >= (uintptr_t)(t166)) gm_trap_index(t165, t166, gm_src2j6ljeja7ilp, 65, 31);
            void * t167 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t165 * 88);
            gm_t5o0tpv5jka2l t168;
            gm_f_expr_porbc9t((void *)&t168, (void *)NULL, cx, NULL, t164, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t168))[0] != 0)) {
                gm_t5o0tpv5jka2l t169;
                GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t169 + 4, 0);
                ((uint8_t *)&t169)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t169 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t168) + 8)));
                F.ret = t169;
                goto gm_fret;
            }
            gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&t168) + 8));
            gm_l15 = t170;
        }
        {
            void * t171 = gm_l0;
            uint64_t t172 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t173 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t172 >= (uintptr_t)(t173)) gm_trap_index(t172, t173, gm_src2j6ljeja7ilp, 66, 31);
            void * t174 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t172 * 88);
            gm_t5o0tpv5jka2l t175;
            gm_f_expr_porbc9t((void *)&t175, (void *)NULL, cx, NULL, t171, t174);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t175))[0] != 0)) {
                gm_t5o0tpv5jka2l t176;
                GM_ST(uint8_t, (uint8_t *)&t176 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t176 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t176 + 4, 0);
                ((uint8_t *)&t176)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t176 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t175) + 8)));
                F.ret = t176;
                goto gm_fret;
            }
            gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&t175) + 8));
            gm_l16 = t177;
        }
        {
            void * t178 = gm_l0;
            gm_t0uuhaljsf410 t179;
            gm_b16_8 t180 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t179 + 0, t180);
            gm_b16_8 t181 = gm_l15;
            GM_ST(gm_b16_8, (uint8_t *)&t179 + 16, t181);
            gm_b16_8 t182 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t179 + 32, t182);
            gm_b16_8 t183 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t179 + 48, t183);
            gm_b0_1 t184 = gm_f_linef_biu9mas((void *)NULL, cx, NULL, t178, &t179);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t185 = gm_l0;
            gm_b16_8 t186 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t185);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t186);
            goto gm_fdone;
        }
        goto me21;
mn154:;
ma32:;
        uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t188 != ((uint8_t)78ULL)) goto mn187;
        {
            void * t191 = gm_l7;
            uint64_t t192 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t192 >= (uintptr_t)(t193)) gm_trap_index(t192, t193, gm_src2j6ljeja7ilp, 71, 41);
            gm_t88rcucg1b7uk t194 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t192 * 88) + 0));
            gm_t53at80pfdus3 t195;
            gm_f_kind_jcnt4hm((void *)&t195, t191, GM_CONV(gm_csguankko4lmhd, t194));
            uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&t195) + 0));
            if (t196 != ((uint8_t)10ULL)) goto ie190;
            gm_t88rcucg1b7uk t197 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t195) + 8) + 4));
            gm_l17 = t197;
            {
                void * t199 = gm_l0;
                uint64_t t200 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t200 >= (uintptr_t)(t201)) gm_trap_index(t200, t201, gm_src2j6ljeja7ilp, 72, 29);
                gm_t88rcucg1b7uk t202 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t200 * 88) + 0));
                void * t203 = gm_l0;
                uint64_t t204 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t205 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t204 >= (uintptr_t)(t205)) gm_trap_index(t204, t205, gm_src2j6ljeja7ilp, 72, 54);
                void * t206 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t204 * 88);
                gm_t5o0tpv5jka2l t207;
                gm_f_expr_porbc9t((void *)&t207, (void *)NULL, cx, NULL, t203, t206);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t207))[0] != 0)) {
                    gm_t5o0tpv5jka2l t208;
                    GM_ST(uint8_t, (uint8_t *)&t208 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t208 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t208 + 4, 0);
                    ((uint8_t *)&t208)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t208 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t207) + 8)));
                    F.ret = t208;
                    goto gm_fret;
                }
                gm_b16_8 t209 = GM_LD(gm_b16_8, (((uint8_t *)&t207) + 8));
                gm_b16_8 t210 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t199, t202, t209);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t210;
            }
            {
                void * t211 = gm_l0;
                uint64_t t212 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t213 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t212 >= (uintptr_t)(t213)) gm_trap_index(t212, t213, gm_src2j6ljeja7ilp, 73, 29);
                gm_t88rcucg1b7uk t214 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t212 * 88) + 0));
                void * t215 = gm_l0;
                uint64_t t216 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t217 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t216 >= (uintptr_t)(t217)) gm_trap_index(t216, t217, gm_src2j6ljeja7ilp, 73, 54);
                void * t218 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t216 * 88);
                gm_t5o0tpv5jka2l t219;
                gm_f_expr_porbc9t((void *)&t219, (void *)NULL, cx, NULL, t215, t218);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t219))[0] != 0)) {
                    gm_t5o0tpv5jka2l t220;
                    GM_ST(uint8_t, (uint8_t *)&t220 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t220 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t220 + 4, 0);
                    ((uint8_t *)&t220)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t220 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t219) + 8)));
                    F.ret = t220;
                    goto gm_fret;
                }
                gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&t219) + 8));
                gm_b16_8 t222 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t211, t214, t221);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t222;
            }
            {
                void * t223 = gm_l0;
                gm_b16_8 t224 = gm_l18;
                gm_b16_8 t225 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t223, t224);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t225;
            }
            {
                void * t226 = gm_l0;
                gm_t0j6181meiu4s t227;
                gm_b16_8 t228 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t227 + 0, t228);
                gm_b16_8 t229 = gm_l18;
                gm_b16_8 t230 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t229);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t227 + 16, t230);
                gm_b16_8 t231 = gm_l19;
                gm_b16_8 t232 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t231);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t227 + 32, t232);
                gm_b16_8 t233 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t227 + 48, t233);
                void * t234 = gm_l0;
                void * t235 = GM_LD(void *, (((uint8_t *)t234) + 0));
                void * t236 = (void *)(((uint8_t *)t235) + 8);
                gm_t88rcucg1b7uk t237 = gm_l17;
                uint64_t t238 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t236, t237);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint64_t, (uint8_t *)&t227 + 64, t238);
                gm_b0_1 t239 = gm_f_linef_jq6ksk1((void *)NULL, cx, NULL, t226, &t227);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t240 = gm_l0;
                gm_b16_8 t241 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t240);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t241);
                goto gm_fdone;
            }
            goto ix190;
ie190:;
            {
                void * t243 = gm_l0;
                gm_b16_8 t244 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t243);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t244);
                goto gm_fdone;
            }
ix190:;
        }
        goto me21;
mn187:;
ma33:;
        goto me21;
mn245:;
ma34:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 39, 5);
me21:;
    }
    {
        gm_f_list_epvducg((void *)&F.l21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l23 = ((uint64_t)0ULL);
        }
        {
            uint64_t t248 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            gm_l22 = t248;
        }
        {
            for (;;) {
                uint64_t t250 = gm_l23;
                uint64_t t251 = gm_l22;
                uint8_t t252 = (t250 < t251);
                if (!t252) goto lb249;
                {
                    uint64_t t255 = gm_l23;
                    uint64_t t258 = GM_LD(uint64_t, ((uint8_t *)&t255));
                    gm_l24 = t258;
                    {
                        void * t260 = (void *)((uint8_t *)&F.l21);
                        void * t261 = gm_l0;
                        uint64_t t262 = gm_l24;
                        uint64_t t263 = (uintptr_t)t262;
                        uint64_t t264 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t263 >= (uintptr_t)(t264)) gm_trap_index(t263, t264, gm_src2j6ljeja7ilp, 81, 51);
                        void * t265 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t263 * 88);
                        gm_t5o0tpv5jka2l t266;
                        gm_f_expr_porbc9t((void *)&t266, (void *)NULL, cx, NULL, t261, t265);
                        if (gm_unw.kind) {
                            goto gm_ll253;
                        }
                        if (((((uint8_t *)&t266))[0] != 0)) {
                            gm_t5o0tpv5jka2l t267;
                            GM_ST(uint8_t, (uint8_t *)&t267 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t267 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t267 + 4, 0);
                            ((uint8_t *)&t267)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t267 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t266) + 8)));
                            F.ret = t267;
                            goto gm_fret;
                        }
                        gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&t266) + 8));
                        gm_tftb1odc739t5 t269 = gm_f_push_3ef746b(t260, t268);
                        if (((((uint8_t *)&t269))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t269) + 1, gm_src2j6ljeja7ilp, 81, 28);
                        gm_b0_1 t270 = GM_LD(gm_b0_1, (((uint8_t *)&t269) + 1));
                    }
                    goto me256;
mn257:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 81, 5);
me256:;
                }
lc249:;
                {
                    uint64_t t272 = gm_l23;
                    uint64_t r273; bool o274 = __builtin_add_overflow(t272, ((uint64_t)1ULL), &r273);
                    uint64_t t275 = r273;
                    gm_l23 = t275;
                }
            }
gm_ll253:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb249; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc249; }
            }
            goto gm_lad;
lb249:;
        }
    }
    {
        uint64_t t276 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t277 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
        if (t276 >= (uintptr_t)(t277)) gm_trap_index(t276, t277, gm_src2j6ljeja7ilp, 82, 11);
        gm_b16_8 t278 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t276 * 16));
        gm_l25 = t278;
    }
    {
        uint64_t t279 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t280 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t279 >= (uintptr_t)(t280)) gm_trap_index(t279, t280, gm_src2j6ljeja7ilp, 83, 11);
        gm_t88rcucg1b7uk t281 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t279 * 88) + 0));
        gm_l26 = t281;
    }
    {
        uint8_t t311 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t311) { case ((uint8_t)0ULL): goto ma283; case ((uint8_t)1ULL): goto ma283; case ((uint8_t)2ULL): goto ma283; case ((uint8_t)3ULL): goto ma283; case ((uint8_t)4ULL): goto ma283; case ((uint8_t)5ULL): goto ma283; case ((uint8_t)6ULL): goto ma283; case ((uint8_t)7ULL): goto ma284; case ((uint8_t)8ULL): goto ma285; case ((uint8_t)9ULL): goto ma286; case ((uint8_t)10ULL): goto ma288; case ((uint8_t)11ULL): goto ma288; case ((uint8_t)12ULL): goto ma288; case ((uint8_t)13ULL): goto ma287; case ((uint8_t)14ULL): goto ma289; case ((uint8_t)15ULL): goto ma290; case ((uint8_t)16ULL): goto ma291; case ((uint8_t)17ULL): goto ma291; case ((uint8_t)18ULL): goto ma292; case ((uint8_t)19ULL): goto ma293; case ((uint8_t)20ULL): goto ma294; case ((uint8_t)21ULL): goto ma303; case ((uint8_t)22ULL): goto ma295; case ((uint8_t)23ULL): goto ma296; case ((uint8_t)24ULL): goto ma297; case ((uint8_t)25ULL): goto ma298; case ((uint8_t)26ULL): goto ma299; case ((uint8_t)27ULL): goto ma300; case ((uint8_t)28ULL): goto ma302; case ((uint8_t)29ULL): goto ma302; case ((uint8_t)30ULL): goto ma302; case ((uint8_t)31ULL): goto ma302; case ((uint8_t)32ULL): goto ma301; case ((uint8_t)33ULL): goto ma301; case ((uint8_t)34ULL): goto ma302; case ((uint8_t)35ULL): goto ma307; case ((uint8_t)36ULL): goto ma309; case ((uint8_t)37ULL): goto ma309; case ((uint8_t)38ULL): goto ma309; case ((uint8_t)39ULL): goto ma309; case ((uint8_t)40ULL): goto ma309; case ((uint8_t)41ULL): goto ma309; case ((uint8_t)42ULL): goto ma309; case ((uint8_t)43ULL): goto ma309; case ((uint8_t)44ULL): goto ma309; case ((uint8_t)45ULL): goto ma309; case ((uint8_t)46ULL): goto ma309; case ((uint8_t)47ULL): goto ma309; case ((uint8_t)48ULL): goto ma309; case ((uint8_t)49ULL): goto ma309; case ((uint8_t)50ULL): goto ma309; case ((uint8_t)51ULL): goto ma309; case ((uint8_t)52ULL): goto ma309; case ((uint8_t)53ULL): goto ma309; case ((uint8_t)54ULL): goto ma309; case ((uint8_t)55ULL): goto ma309; case ((uint8_t)56ULL): goto ma309; case ((uint8_t)57ULL): goto ma309; case ((uint8_t)58ULL): goto ma309; case ((uint8_t)59ULL): goto ma309; case ((uint8_t)60ULL): goto ma304; case ((uint8_t)61ULL): goto ma309; case ((uint8_t)62ULL): goto ma308; case ((uint8_t)63ULL): goto ma309; case ((uint8_t)64ULL): goto ma305; case ((uint8_t)65ULL): goto ma305; case ((uint8_t)66ULL): goto ma305; case ((uint8_t)67ULL): goto ma305; case ((uint8_t)68ULL): goto ma305; case ((uint8_t)69ULL): goto ma306; case ((uint8_t)70ULL): goto ma305; case ((uint8_t)71ULL): goto ma305; case ((uint8_t)72ULL): goto ma309; case ((uint8_t)73ULL): goto ma309; case ((uint8_t)74ULL): goto ma309; case ((uint8_t)75ULL): goto ma309; case ((uint8_t)76ULL): goto ma309; case ((uint8_t)77ULL): goto ma309; case ((uint8_t)78ULL): goto ma309; case ((uint8_t)79ULL): goto ma309; case ((uint8_t)80ULL): goto ma309; case ((uint8_t)81ULL): goto ma309; default: goto ma309; }
ma283:;
        uint8_t t315 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t315 != ((uint8_t)0ULL)) goto pa314;
        goto po313;
pa314:;
        uint8_t t317 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t317 != ((uint8_t)1ULL)) goto pa316;
        goto po313;
pa316:;
        uint8_t t319 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t319 != ((uint8_t)2ULL)) goto pa318;
        goto po313;
pa318:;
        uint8_t t321 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t321 != ((uint8_t)3ULL)) goto pa320;
        goto po313;
pa320:;
        uint8_t t323 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t323 != ((uint8_t)4ULL)) goto pa322;
        goto po313;
pa322:;
        uint8_t t325 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t325 != ((uint8_t)5ULL)) goto pa324;
        goto po313;
pa324:;
        uint8_t t327 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t327 != ((uint8_t)6ULL)) goto pa326;
        goto po313;
pa326:;
        goto mn312;
po313:;
        void * t328 = gm_l0;
        void * t329 = gm_l1;
        gm_tvdg94bk7aa22 t330 = gm_l2;
        gm_t88rcucg1b7uk t331 = gm_l26;
        gm_b16_8 t332 = gm_l25;
        gm_t5o0tpv5jka2l t333;
        gm_f_convert_lqrk2n0((void *)&t333, (void *)NULL, cx, NULL, t328, t329, t330, t331, t332);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t333))[0] != 0)) {
            gm_t5o0tpv5jka2l t334;
            GM_ST(uint8_t, (uint8_t *)&t334 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t334 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t334 + 4, 0);
            ((uint8_t *)&t334)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t334 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t333) + 8)));
            F.ret = t334;
            goto gm_fret;
        }
        gm_b16_8 t335 = GM_LD(gm_b16_8, (((uint8_t *)&t333) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t335);
        goto gm_fdone;
        goto me282;
mn312:;
ma284:;
        uint8_t t337 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t337 != ((uint8_t)7ULL)) goto mn336;
        {
            void * t339 = gm_l0;
            gm_t88rcucg1b7uk t340 = gm_l26;
            gm_b16_8 t341 = gm_l25;
            gm_b16_8 t342 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t339, t340, t341);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t342;
        }
        {
            void * t343 = gm_l0;
            gm_t88rcucg1b7uk t344 = gm_l5;
            gm_b16_8 t345 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t343, t344);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l28 = t345;
        }
        {
            void * t346 = gm_l0;
            void * t347 = GM_LD(void *, (((uint8_t *)t346) + 0));
            void * t348 = (void *)(((uint8_t *)t347) + 8);
            gm_t88rcucg1b7uk t349 = gm_l26;
            uint64_t t350 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t348, t349);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l29 = t350;
        }
        {
            void * t351 = gm_l0;
            gm_tsroeipraj6j8 t352;
            gm_b16_8 t353 = gm_l28;
            GM_ST(gm_b16_8, (uint8_t *)&t352 + 0, t353);
            gm_b16_8 t354 = gm_l28;
            GM_ST(gm_b16_8, (uint8_t *)&t352 + 16, t354);
            gm_b16_8 t355 = gm_l28;
            GM_ST(gm_b16_8, (uint8_t *)&t352 + 32, t355);
            gm_b16_8 t356 = gm_l27;
            GM_ST(gm_b16_8, (uint8_t *)&t352 + 48, t356);
            gm_b16_8 t357 = gm_l28;
            GM_ST(gm_b16_8, (uint8_t *)&t352 + 64, t357);
            uint64_t t358 = gm_l29;
            GM_ST(uint64_t, (uint8_t *)&t352 + 80, t358);
            gm_b16_8 t359 = gm_l28;
            GM_ST(gm_b16_8, (uint8_t *)&t352 + 88, t359);
            uint64_t t360 = gm_l29;
            GM_ST(uint64_t, (uint8_t *)&t352 + 104, t360);
            gm_b0_1 t361 = gm_f_linef_a98fois((void *)NULL, cx, NULL, t351, &t352);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t363 = gm_l0;
            gm_t88rcucg1b7uk t364 = gm_l5;
            gm_tqcc6dmlne11b t365;
            gm_f_int_info_2tcdf1n((void *)&t365, (void *)NULL, cx, NULL, t363, t364);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t365))[16] != 0)) goto ie362;
            gm_tuglhjjmj2g58 t366 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t365));
            gm_l30 = t366;
            {
                gm_tuglhjjmj2g58 t369 = gm_l30;
                uint8_t t370 = gm_f_ii_exact_g6a19dp(GM_CONV(gm_cs2lqoac0a5ng4, t369));
                uint8_t t371 = (uint8_t)!t370;
                if (!t371) goto ie368;
                {
                    gm_tuglhjjmj2g58 t373 = gm_l30;
                    gm_b16_8 t374 = gm_l28;
                    gm_b16_8 t375 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t373, t374);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l31 = t375;
                }
                {
                    void * t376 = gm_l0;
                    gm_t6j6smj7qoctu t377;
                    gm_b16_8 t378 = gm_l28;
                    GM_ST(gm_b16_8, (uint8_t *)&t377 + 0, t378);
                    gm_b16_8 t379 = gm_l31;
                    GM_ST(gm_b16_8, (uint8_t *)&t377 + 16, t379);
                    gm_b0_1 t380 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t376, &t377);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix368;
ie368:;
ix368:;
            }
            goto ix362;
ie362:;
ix362:;
        }
        {
            gm_b16_8 t381 = gm_l28;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t381);
            goto gm_fdone;
        }
        goto me282;
mn336:;
ma285:;
        uint8_t t383 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t383 != ((uint8_t)8ULL)) goto mn382;
        {
            void * t385 = gm_l0;
            gm_t88rcucg1b7uk t386 = gm_l5;
            uint64_t t387 = gm_f_arith_pointee_align_vqoa1mk((void *)NULL, cx, NULL, t385, t386);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l32 = t387;
        }
        {
            uint64_t t389 = gm_l32;
            uint8_t t390 = (t389 > ((uint64_t)1ULL));
            if (!t390) goto ie388;
            void * t391 = gm_l0;
            void * t392 = GM_LD(void *, (((uint8_t *)t391) + 0));
            uint8_t t393 = GM_LD(uint8_t, ((((uint8_t *)t392) + 1136) + 0));
            if (!t393) goto ie388;
            void * t394 = gm_l0;
            gm_t88rcucg1b7uk t395 = gm_l26;
            uint64_t t396 = gm_f_arith_pointee_align_vqoa1mk((void *)NULL, cx, NULL, t394, t395);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t397 = gm_l32;
            uint8_t t398 = (t396 < t397);
            if (!t398) goto ie388;
            {
                void * t400 = gm_l0;
                gm_tpm6m3o0hprmc t401;
                gm_b16_8 t402 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t401 + 0, t402);
                uint64_t t403 = gm_l32;
                GM_ST(uint64_t, (uint8_t *)&t401 + 16, t403);
                gm_b16_8 t404 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t401 + 24, t404);
                uint64_t t405 = gm_l32;
                GM_ST(uint64_t, (uint8_t *)&t401 + 40, t405);
                gm_b16_8 t406 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t401 + 48, t406);
                gm_b0_1 t407 = gm_f_linef_kafc4c5((void *)NULL, cx, NULL, t400, &t401);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix388;
ie388:;
ix388:;
        }
        {
            void * t408 = gm_l0;
            gm_t88rcucg1b7uk t409 = gm_l5;
            gm_b16_8 t410 = gm_l25;
            gm_b16_8 t411 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t408, t409, t410);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t411);
            goto gm_fdone;
        }
        goto me282;
mn382:;
ma286:;
        uint8_t t413 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t413 != ((uint8_t)9ULL)) goto mn412;
        void * t414 = gm_l0;
        gm_t88rcucg1b7uk t415 = gm_l5;
        gm_b16_8 t416 = gm_l25;
        gm_tnpifv20irnph t417;
        GM_ST(gm_b16_8, ((uint8_t *)&t417 + 0), t416);
        gm_tabb710d9mq2e t418;
        gm_f_format_6df13cd((void *)&t418, (void *)NULL, cx, NULL, t417);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t418))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t418) + 8, gm_src2j6ljeja7ilp, 107, 35);
        gm_b16_8 t419 = GM_LD(gm_b16_8, (((uint8_t *)&t418) + 8));
        gm_b16_8 t420 = GM_LD(gm_b16_8, ((uint8_t *)&t419));
        gm_b16_8 t421 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t414, t415, t420);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t421);
        goto gm_fdone;
        goto me282;
mn412:;
ma287:;
        uint8_t t423 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t423 != ((uint8_t)13ULL)) goto mn422;
        void * t424 = gm_l0;
        gm_t88rcucg1b7uk t425 = gm_l5;
        gm_b16_8 t426 = gm_l25;
        gm_tnpifv20irnph t427;
        GM_ST(gm_b16_8, ((uint8_t *)&t427 + 0), t426);
        gm_tabb710d9mq2e t428;
        gm_f_format_a7bth9i((void *)&t428, (void *)NULL, cx, NULL, t427);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t428))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t428) + 8, gm_src2j6ljeja7ilp, 108, 36);
        gm_b16_8 t429 = GM_LD(gm_b16_8, (((uint8_t *)&t428) + 8));
        gm_b16_8 t430 = GM_LD(gm_b16_8, ((uint8_t *)&t429));
        gm_b16_8 t431 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t424, t425, t430);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t431);
        goto gm_fdone;
        goto me282;
mn422:;
ma288:;
        uint8_t t435 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t435 != ((uint8_t)10ULL)) goto pa434;
        goto po433;
pa434:;
        uint8_t t437 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t437 != ((uint8_t)11ULL)) goto pa436;
        goto po433;
pa436:;
        uint8_t t439 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t439 != ((uint8_t)12ULL)) goto pa438;
        goto po433;
pa438:;
        goto mn432;
po433:;
        {
            void * t442 = gm_l0;
            void * t443 = GM_LD(void *, (((uint8_t *)t442) + 0));
            void * t444 = (void *)(((uint8_t *)t443) + 8);
            gm_t88rcucg1b7uk t445 = gm_l26;
            gm_t53at80pfdus3 t446;
            gm_f_kind_ghvav22((void *)&t446, t444, GM_CONV(gm_csguankko4lmhd, t445));
            uint8_t t447 = GM_LD(uint8_t, (((uint8_t *)&t446) + 0));
            if (t447 != ((uint8_t)9ULL)) goto ie441;
            gm_t88rcucg1b7uk t448 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t446) + 8) + 4));
            gm_l33 = t448;
            {
                void * t450 = gm_l0;
                void * t451 = GM_LD(void *, (((uint8_t *)t450) + 0));
                void * t452 = (void *)(((uint8_t *)t451) + 8);
                gm_t88rcucg1b7uk t453 = gm_l33;
                uint64_t t454 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t452, t453);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l34 = t454;
            }
            {
                gm_b16_8 t455 = gm_l25;
                gm_l35 = t455;
            }
            {
                uint64_t t456 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t457 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
                if (t456 >= (uintptr_t)(t457)) gm_trap_index(t456, t457, gm_src2j6ljeja7ilp, 113, 18);
                gm_b16_8 t458 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t456 * 16));
                gm_l36 = t458;
            }
            {
                uint8_t t460 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)12ULL)));
                if (!t460) goto ie459;
                {
                    uint64_t t463 = gm_l34;
                    uint8_t t464 = ((t463 == ((uint64_t)0ULL)));
                    if (!t464) goto ie462;
                    {
                        void * t466 = gm_l0;
                        gm_t88rcucg1b7uk t467 = gm_l5;
                        gm_b16_8 t468;
                        GM_ST(void *, (uint8_t *)&t468, (void *)(gm_sicgmpca63jln));
                        GM_ST(uintptr_t, (uint8_t *)&t468 + 8, (uintptr_t)(1));
                        gm_b16_8 t469 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t466, t467, t468);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t469);
                        goto gm_fdone;
                    }
                    goto ix462;
ie462:;
ix462:;
                }
                {
                    void * t470 = gm_l0;
                    gm_t88rcucg1b7uk t471 = gm_l5;
                    gm_tftksr4op114e t472;
                    gm_b16_8 t473 = gm_l35;
                    GM_ST(gm_b16_8, (uint8_t *)&t472 + 0, t473);
                    gm_b16_8 t474 = gm_l36;
                    GM_ST(gm_b16_8, (uint8_t *)&t472 + 16, t474);
                    uint64_t t475 = gm_l34;
                    GM_ST(uint64_t, (uint8_t *)&t472 + 32, t475);
                    gm_tabb710d9mq2e t476;
                    gm_f_format_ms7m2d2((void *)&t476, (void *)NULL, cx, NULL, &t472);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t476))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t476) + 8, gm_src2j6ljeja7ilp, 116, 34);
                    gm_b16_8 t477 = GM_LD(gm_b16_8, (((uint8_t *)&t476) + 8));
                    gm_b16_8 t478 = GM_LD(gm_b16_8, ((uint8_t *)&t477));
                    gm_b16_8 t479 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t470, t471, t478);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t479);
                    goto gm_fdone;
                }
                goto ix459;
ie459:;
ix459:;
            }
            {
                gm_b16_8 t480;
                uint8_t t482 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)10ULL)));
                if (!t482) goto ie481;
                gm_b16_8 t484;
                GM_ST(void *, (uint8_t *)&t484, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t484 + 8, (uintptr_t)(0));
                gm_b16_8 t485 = t484;
                t480 = t485;
                goto ix481;
ie481:;
                gm_b16_8 t487;
                GM_ST(void *, (uint8_t *)&t487, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t487 + 8, (uintptr_t)(1));
                gm_b16_8 t488 = t487;
                t480 = t488;
ix481:;
                gm_l37 = t480;
            }
            {
                void * t489 = gm_l0;
                gm_t88rcucg1b7uk t490 = gm_l5;
                gm_tttl9nsdo1iu6 t491;
                gm_b16_8 t492 = gm_l35;
                GM_ST(gm_b16_8, (uint8_t *)&t491 + 0, t492);
                gm_b16_8 t493 = gm_l37;
                GM_ST(gm_b16_8, (uint8_t *)&t491 + 16, t493);
                gm_b16_8 t494 = gm_l36;
                GM_ST(gm_b16_8, (uint8_t *)&t491 + 32, t494);
                uint64_t t495 = gm_l34;
                GM_ST(uint64_t, (uint8_t *)&t491 + 48, t495);
                gm_tabb710d9mq2e t496;
                gm_f_format_83msomh((void *)&t496, (void *)NULL, cx, NULL, &t491);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t496))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t496) + 8, gm_src2j6ljeja7ilp, 119, 30);
                gm_b16_8 t497 = GM_LD(gm_b16_8, (((uint8_t *)&t496) + 8));
                gm_b16_8 t498 = GM_LD(gm_b16_8, ((uint8_t *)&t497));
                gm_b16_8 t499 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t489, t490, t498);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t499);
                goto gm_fdone;
            }
            goto ix441;
ie441:;
            {
                gm_trap("reached `@unreachable()`", gm_src2j6ljeja7ilp, 110, 56);
            }
ix441:;
        }
        goto me282;
mn432:;
ma289:;
        uint8_t t502 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t502 != ((uint8_t)14ULL)) goto mn501;
        {
            void * t504 = gm_l0;
            gm_t88rcucg1b7uk t505 = gm_l26;
            gm_b16_8 t506 = gm_l25;
            gm_b16_8 t507 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t504, t505, t506);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l38 = t507;
        }
        {
            void * t508 = gm_l0;
            gm_t88rcucg1b7uk t509 = gm_l5;
            gm_b16_8 t510 = gm_l38;
            gm_b16_8 t511 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t508, t509, t510);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t511);
            goto gm_fdone;
        }
        goto me282;
mn501:;
ma290:;
        uint8_t t513 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t513 != ((uint8_t)15ULL)) goto mn512;
        void * t514 = gm_l0;
        void * t515 = gm_l1;
        gm_t88rcucg1b7uk t516 = gm_l26;
        gm_b16_8 t517 = gm_l25;
        gm_f_from_tag_2m7nfln((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t514, t515, t516, t517);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me282;
mn512:;
ma291:;
        uint8_t t521 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t521 != ((uint8_t)16ULL)) goto pa520;
        goto po519;
pa520:;
        uint8_t t523 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t523 != ((uint8_t)17ULL)) goto pa522;
        goto po519;
pa522:;
        goto mn518;
po519:;
        {
            gm_b16_8 t525;
            uint8_t t527 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)16ULL)));
            if (!t527) goto ie526;
            gm_b16_8 t529;
            GM_ST(void *, (uint8_t *)&t529, (void *)(gm_s1f440st4mnkp));
            GM_ST(uintptr_t, (uint8_t *)&t529 + 8, (uintptr_t)(1));
            gm_b16_8 t530 = t529;
            t525 = t530;
            goto ix526;
ie526:;
            gm_b16_8 t532;
            GM_ST(void *, (uint8_t *)&t532, (void *)(gm_sm5be4659ulci));
            GM_ST(uintptr_t, (uint8_t *)&t532 + 8, (uintptr_t)(1));
            gm_b16_8 t533 = t532;
            t525 = t533;
ix526:;
            gm_l39 = t525;
        }
        {
            void * t535 = gm_l0;
            void * t536 = GM_LD(void *, (((uint8_t *)t535) + 0));
            void * t537 = (void *)(((uint8_t *)t536) + 8);
            gm_t88rcucg1b7uk t538 = gm_l26;
            gm_t5v8n6qrn4rh4 t539;
            gm_f_repr_53quqki((void *)&t539, (void *)NULL, cx, NULL, t537, t538);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t540;
            uint8_t t543 = GM_LD(uint8_t, (((uint8_t *)&t539) + 0));
            if (t543 != ((uint8_t)1ULL)) goto mn542;
            t540 = ((uint8_t)1);
            goto me541;
mn542:;
            t540 = ((uint8_t)0);
            goto me541;
mn544:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 128, 16);
me541:;
            if (!t540) goto ie534;
            {
                gm_b16_8 t546;
                uint8_t t548 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)16ULL)));
                if (!t548) goto ie547;
                gm_b16_8 t550;
                GM_ST(void *, (uint8_t *)&t550, (void *)(gm_s12ng38ve3205));
                GM_ST(uintptr_t, (uint8_t *)&t550 + 8, (uintptr_t)(7));
                gm_b16_8 t551 = t550;
                t546 = t551;
                goto ix547;
ie547:;
                gm_b16_8 t553;
                GM_ST(void *, (uint8_t *)&t553, (void *)(gm_sgjc0ebf56hf4));
                GM_ST(uintptr_t, (uint8_t *)&t553 + 8, (uintptr_t)(7));
                gm_b16_8 t554 = t553;
                t546 = t554;
ix547:;
                gm_l40 = t546;
            }
            {
                void * t555 = gm_l0;
                gm_t88rcucg1b7uk t556 = gm_l5;
                gm_tfeab279c16rr t557;
                gm_b16_8 t558 = gm_l40;
                GM_ST(gm_b16_8, (uint8_t *)&t557 + 0, t558);
                gm_b16_8 t559 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t557 + 16, t559);
                uint64_t t560 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t561 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
                if (t560 >= (uintptr_t)(t561)) gm_trap_index(t560, t561, gm_src2j6ljeja7ilp, 130, 87);
                gm_b16_8 t562 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t560 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t557 + 32, t562);
                gm_tabb710d9mq2e t563;
                gm_f_format_lqnh3lt((void *)&t563, (void *)NULL, cx, NULL, &t557);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t563))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t563) + 8, gm_src2j6ljeja7ilp, 130, 39);
                gm_b16_8 t564 = GM_LD(gm_b16_8, (((uint8_t *)&t563) + 8));
                gm_b16_8 t565 = GM_LD(gm_b16_8, ((uint8_t *)&t564));
                gm_b16_8 t566 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t555, t556, t565);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l41 = t566;
            }
            {
                void * t567 = gm_l0;
                gm_t88rcucg1b7uk t568 = gm_l5;
                gm_b16_8 t569 = gm_l41;
                gm_b16_8 t570 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t567, t568, t569);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t570);
                goto gm_fdone;
            }
            goto ix534;
ie534:;
ix534:;
        }
        {
            void * t571 = gm_l0;
            gm_t88rcucg1b7uk t572 = gm_l5;
            gm_tud7oovfvnf43 t573;
            gm_b16_8 t574 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t573 + 0, t574);
            gm_b16_8 t575 = gm_l39;
            GM_ST(gm_b16_8, (uint8_t *)&t573 + 16, t575);
            uint64_t t576 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t577 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t576 >= (uintptr_t)(t577)) gm_trap_index(t576, t577, gm_src2j6ljeja7ilp, 133, 73);
            gm_b16_8 t578 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t576 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t573 + 32, t578);
            gm_b16_8 t579 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t573 + 48, t579);
            uint64_t t580 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t581 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t580 >= (uintptr_t)(t581)) gm_trap_index(t580, t581, gm_src2j6ljeja7ilp, 133, 92);
            gm_b16_8 t582 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t580 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t573 + 64, t582);
            gm_tabb710d9mq2e t583;
            gm_f_format_eqjcikj((void *)&t583, (void *)NULL, cx, NULL, &t573);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t583))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t583) + 8, gm_src2j6ljeja7ilp, 133, 30);
            gm_b16_8 t584 = GM_LD(gm_b16_8, (((uint8_t *)&t583) + 8));
            gm_b16_8 t585 = GM_LD(gm_b16_8, ((uint8_t *)&t584));
            gm_b16_8 t586 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t571, t572, t585);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t586);
            goto gm_fdone;
        }
        goto me282;
mn518:;
ma292:;
        uint8_t t588 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t588 != ((uint8_t)18ULL)) goto mn587;
        {
            gm_b16_8 t590 = gm_l25;
            gm_l42 = t590;
        }
        {
            uint64_t t591 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t592 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t591 >= (uintptr_t)(t592)) gm_trap_index(t591, t592, gm_src2j6ljeja7ilp, 137, 19);
            gm_b16_8 t593 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t591 * 16));
            gm_l43 = t593;
        }
        {
            uint64_t t594 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t595 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t594 >= (uintptr_t)(t595)) gm_trap_index(t594, t595, gm_src2j6ljeja7ilp, 138, 19);
            gm_b16_8 t596 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t594 * 16));
            gm_l44 = t596;
        }
        {
            void * t598 = gm_l0;
            void * t599 = GM_LD(void *, (((uint8_t *)t598) + 0));
            void * t600 = (void *)(((uint8_t *)t599) + 8);
            gm_t88rcucg1b7uk t601 = gm_l26;
            gm_t5v8n6qrn4rh4 t602;
            gm_f_repr_53quqki((void *)&t602, (void *)NULL, cx, NULL, t600, t601);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t603;
            uint8_t t606 = GM_LD(uint8_t, (((uint8_t *)&t602) + 0));
            if (t606 != ((uint8_t)1ULL)) goto mn605;
            t603 = ((uint8_t)1);
            goto me604;
mn605:;
            t603 = ((uint8_t)0);
            goto me604;
mn607:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 139, 16);
me604:;
            if (!t603) goto ie597;
            {
                void * t609 = gm_l0;
                gm_t88rcucg1b7uk t610 = gm_l5;
                gm_tfeab279c16rr t611;
                gm_b16_8 t612 = gm_l42;
                GM_ST(gm_b16_8, (uint8_t *)&t611 + 0, t612);
                gm_b16_8 t613 = gm_l43;
                GM_ST(gm_b16_8, (uint8_t *)&t611 + 16, t613);
                gm_b16_8 t614 = gm_l44;
                GM_ST(gm_b16_8, (uint8_t *)&t611 + 32, t614);
                gm_tabb710d9mq2e t615;
                gm_f_format_c57v5iu((void *)&t615, (void *)NULL, cx, NULL, &t611);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t615))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t615) + 8, gm_src2j6ljeja7ilp, 140, 39);
                gm_b16_8 t616 = GM_LD(gm_b16_8, (((uint8_t *)&t615) + 8));
                gm_b16_8 t617 = GM_LD(gm_b16_8, ((uint8_t *)&t616));
                gm_b16_8 t618 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t609, t610, t617);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l45 = t618;
            }
            {
                void * t619 = gm_l0;
                gm_t88rcucg1b7uk t620 = gm_l5;
                gm_b16_8 t621 = gm_l45;
                gm_b16_8 t622 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t619, t620, t621);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t622);
                goto gm_fdone;
            }
            goto ix597;
ie597:;
ix597:;
        }
        {
            void * t623 = gm_l0;
            gm_t88rcucg1b7uk t624 = gm_l5;
            gm_th2d9c7aju8hp t625;
            gm_b16_8 t626 = gm_l42;
            GM_ST(gm_b16_8, (uint8_t *)&t625 + 0, t626);
            gm_b16_8 t627 = gm_l43;
            GM_ST(gm_b16_8, (uint8_t *)&t625 + 16, t627);
            gm_b16_8 t628 = gm_l43;
            GM_ST(gm_b16_8, (uint8_t *)&t625 + 32, t628);
            gm_b16_8 t629 = gm_l42;
            GM_ST(gm_b16_8, (uint8_t *)&t625 + 48, t629);
            gm_b16_8 t630 = gm_l44;
            GM_ST(gm_b16_8, (uint8_t *)&t625 + 64, t630);
            gm_b16_8 t631 = gm_l44;
            GM_ST(gm_b16_8, (uint8_t *)&t625 + 80, t631);
            gm_b16_8 t632 = gm_l42;
            GM_ST(gm_b16_8, (uint8_t *)&t625 + 96, t632);
            gm_tabb710d9mq2e t633;
            gm_f_format_jpvl2js((void *)&t633, (void *)NULL, cx, NULL, &t625);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t633))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t633) + 8, gm_src2j6ljeja7ilp, 143, 30);
            gm_b16_8 t634 = GM_LD(gm_b16_8, (((uint8_t *)&t633) + 8));
            gm_b16_8 t635 = GM_LD(gm_b16_8, ((uint8_t *)&t634));
            gm_b16_8 t636 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t623, t624, t635);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t636);
            goto gm_fdone;
        }
        goto me282;
mn587:;
ma293:;
        uint8_t t638 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t638 != ((uint8_t)19ULL)) goto mn637;
        {
            void * t641 = gm_l0;
            void * t642 = GM_LD(void *, (((uint8_t *)t641) + 0));
            void * t643 = (void *)(((uint8_t *)t642) + 8);
            gm_t88rcucg1b7uk t644 = gm_l26;
            gm_t5v8n6qrn4rh4 t645;
            gm_f_repr_53quqki((void *)&t645, (void *)NULL, cx, NULL, t643, t644);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t646;
            uint8_t t649 = GM_LD(uint8_t, (((uint8_t *)&t645) + 0));
            if (t649 != ((uint8_t)1ULL)) goto mn648;
            t646 = ((uint8_t)1);
            goto me647;
mn648:;
            t646 = ((uint8_t)0);
            goto me647;
mn650:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 146, 16);
me647:;
            if (!t646) goto ie640;
            {
                void * t652 = gm_l0;
                gm_t88rcucg1b7uk t653 = gm_l5;
                gm_tud7oovfvnf43 t654;
                gm_b16_8 t655 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t654 + 0, t655);
                gm_b16_8 t656 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t654 + 16, t656);
                gm_b16_8 t657 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t654 + 32, t657);
                gm_b16_8 t658 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t654 + 48, t658);
                gm_b16_8 t659 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t654 + 64, t659);
                gm_tabb710d9mq2e t660;
                gm_f_format_3j59e2u((void *)&t660, (void *)NULL, cx, NULL, &t654);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t660))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t660) + 8, gm_src2j6ljeja7ilp, 147, 34);
                gm_b16_8 t661 = GM_LD(gm_b16_8, (((uint8_t *)&t660) + 8));
                gm_b16_8 t662 = GM_LD(gm_b16_8, ((uint8_t *)&t661));
                gm_b16_8 t663 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t652, t653, t662);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t663);
                goto gm_fdone;
            }
            goto ix640;
ie640:;
ix640:;
        }
        {
            void * t664 = gm_l0;
            gm_t88rcucg1b7uk t665 = gm_l26;
            gm_tqcc6dmlne11b t666;
            gm_f_int_info_2tcdf1n((void *)&t666, (void *)NULL, cx, NULL, t664, t665);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t666))[16] != 0)) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 149, 19);
            gm_tuglhjjmj2g58 t667 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t666));
            gm_l46 = t667;
        }
        {
            uint8_t t669 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
            if (!t669) goto ie668;
            void * t670 = gm_l0;
            void * t671 = GM_LD(void *, (((uint8_t *)t670) + 0));
            uint8_t t672 = GM_LD(uint8_t, ((((uint8_t *)t671) + 1136) + 0));
            if (!t672) goto ie668;
            {
                void * t674 = gm_l0;
                gm_t88rcucg1b7uk t675 = gm_l26;
                gm_tuglhjjmj2g58 t676 = gm_l46;
                gm_tbci5ospk5nnn t677;
                gm_f_ii_min_lku7pdi((void *)&t677, (void *)NULL, cx, NULL, t676);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t678 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t674, t675, &t677);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l47 = t678;
            }
            {
                void * t679 = gm_l0;
                gm_tfeab279c16rr t680;
                gm_b16_8 t681 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t680 + 0, t681);
                gm_b16_8 t682 = gm_l47;
                GM_ST(gm_b16_8, (uint8_t *)&t680 + 16, t682);
                gm_b16_8 t683 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t680 + 32, t683);
                gm_b0_1 t684 = gm_f_linef_qno93os((void *)NULL, cx, NULL, t679, &t680);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix668;
ie668:;
ix668:;
        }
        {
            void * t685 = gm_l0;
            gm_t88rcucg1b7uk t686 = gm_l5;
            gm_tfeab279c16rr t687;
            gm_b16_8 t688 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t687 + 0, t688);
            gm_b16_8 t689 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t687 + 16, t689);
            gm_b16_8 t690 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t687 + 32, t690);
            gm_tabb710d9mq2e t691;
            gm_f_format_iqjnutf((void *)&t691, (void *)NULL, cx, NULL, &t687);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t691))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t691) + 8, gm_src2j6ljeja7ilp, 154, 30);
            gm_b16_8 t692 = GM_LD(gm_b16_8, (((uint8_t *)&t691) + 8));
            gm_b16_8 t693 = GM_LD(gm_b16_8, ((uint8_t *)&t692));
            gm_b16_8 t694 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t685, t686, t693);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t694);
            goto gm_fdone;
        }
        goto me282;
mn637:;
ma294:;
        uint8_t t696 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t696 != ((uint8_t)20ULL)) goto mn695;
        {
            void * t698 = gm_l0;
            gm_t88rcucg1b7uk t699 = gm_l26;
            gm_tqcc6dmlne11b t700;
            gm_f_int_info_2tcdf1n((void *)&t700, (void *)NULL, cx, NULL, t698, t699);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t700))[16] != 0)) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 157, 19);
            gm_tuglhjjmj2g58 t701 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t700));
            gm_l48 = t701;
        }
        {
            gm_tuglhjjmj2g58 t702 = gm_l48;
            gm_b16_8 t703 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t702));
            gm_l49 = t703;
        }
        {
            gm_b16_8 t704 = gm_l25;
            gm_l50 = t704;
        }
        {
            uint64_t t705 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t706 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t705 >= (uintptr_t)(t706)) gm_trap_index(t705, t706, gm_src2j6ljeja7ilp, 160, 18);
            gm_b16_8 t707 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t705 * 16));
            gm_l51 = t707;
        }
        {
            void * t708 = gm_l0;
            gm_t88rcucg1b7uk t709 = gm_l5;
            gm_tk6rvhh54g86p t710;
            gm_b16_8 t711 = gm_l50;
            GM_ST(gm_b16_8, (uint8_t *)&t710 + 0, t711);
            gm_b16_8 t712 = gm_l51;
            GM_ST(gm_b16_8, (uint8_t *)&t710 + 16, t712);
            gm_b16_8 t713 = gm_l49;
            GM_ST(gm_b16_8, (uint8_t *)&t710 + 32, t713);
            gm_b16_8 t714 = gm_l50;
            GM_ST(gm_b16_8, (uint8_t *)&t710 + 48, t714);
            gm_b16_8 t715 = gm_l49;
            GM_ST(gm_b16_8, (uint8_t *)&t710 + 64, t715);
            gm_b16_8 t716 = gm_l51;
            GM_ST(gm_b16_8, (uint8_t *)&t710 + 80, t716);
            gm_b16_8 t717 = gm_l49;
            GM_ST(gm_b16_8, (uint8_t *)&t710 + 96, t717);
            gm_b16_8 t718 = gm_l51;
            GM_ST(gm_b16_8, (uint8_t *)&t710 + 112, t718);
            gm_b16_8 t719 = gm_l49;
            GM_ST(gm_b16_8, (uint8_t *)&t710 + 128, t719);
            gm_b16_8 t720 = gm_l50;
            GM_ST(gm_b16_8, (uint8_t *)&t710 + 144, t720);
            gm_tabb710d9mq2e t721;
            gm_f_format_53mv29j((void *)&t721, (void *)NULL, cx, NULL, &t710);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t721))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t721) + 8, gm_src2j6ljeja7ilp, 161, 30);
            gm_b16_8 t722 = GM_LD(gm_b16_8, (((uint8_t *)&t721) + 8));
            gm_b16_8 t723 = GM_LD(gm_b16_8, ((uint8_t *)&t722));
            gm_b16_8 t724 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t708, t709, t723);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t724);
            goto gm_fdone;
        }
        goto me282;
mn695:;
ma295:;
        uint8_t t726 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t726 != ((uint8_t)22ULL)) goto mn725;
        void * t727 = gm_l0;
        gm_t88rcucg1b7uk t728 = gm_l5;
        gm_b16_8 t729 = gm_l25;
        gm_tnpifv20irnph t730;
        GM_ST(gm_b16_8, ((uint8_t *)&t730 + 0), t729);
        gm_tabb710d9mq2e t731;
        gm_f_format_o73frof((void *)&t731, (void *)NULL, cx, NULL, t730);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t731))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t731) + 8, gm_src2j6ljeja7ilp, 163, 36);
        gm_b16_8 t732 = GM_LD(gm_b16_8, (((uint8_t *)&t731) + 8));
        gm_b16_8 t733 = GM_LD(gm_b16_8, ((uint8_t *)&t732));
        gm_b16_8 t734 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t727, t728, t733);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t734);
        goto gm_fdone;
        goto me282;
mn725:;
ma296:;
        uint8_t t736 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t736 != ((uint8_t)23ULL)) goto mn735;
        void * t737 = gm_l0;
        gm_t88rcucg1b7uk t738 = gm_l5;
        gm_b16_8 t739 = gm_l25;
        gm_tnpifv20irnph t740;
        GM_ST(gm_b16_8, ((uint8_t *)&t740 + 0), t739);
        gm_tabb710d9mq2e t741;
        gm_f_format_ljsk7ji((void *)&t741, (void *)NULL, cx, NULL, t740);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t741))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t741) + 8, gm_src2j6ljeja7ilp, 164, 36);
        gm_b16_8 t742 = GM_LD(gm_b16_8, (((uint8_t *)&t741) + 8));
        gm_b16_8 t743 = GM_LD(gm_b16_8, ((uint8_t *)&t742));
        gm_b16_8 t744 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t737, t738, t743);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t744);
        goto gm_fdone;
        goto me282;
mn735:;
ma297:;
        uint8_t t746 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t746 != ((uint8_t)24ULL)) goto mn745;
        void * t747 = gm_l0;
        gm_t88rcucg1b7uk t748 = gm_l5;
        gm_b16_8 t749 = gm_l25;
        gm_tnpifv20irnph t750;
        GM_ST(gm_b16_8, ((uint8_t *)&t750 + 0), t749);
        gm_tabb710d9mq2e t751;
        gm_f_format_jj2aipm((void *)&t751, (void *)NULL, cx, NULL, t750);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t751))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t751) + 8, gm_src2j6ljeja7ilp, 165, 39);
        gm_b16_8 t752 = GM_LD(gm_b16_8, (((uint8_t *)&t751) + 8));
        gm_b16_8 t753 = GM_LD(gm_b16_8, ((uint8_t *)&t752));
        gm_b16_8 t754 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t747, t748, t753);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t754);
        goto gm_fdone;
        goto me282;
mn745:;
ma298:;
        uint8_t t756 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t756 != ((uint8_t)25ULL)) goto mn755;
        void * t757 = gm_l0;
        gm_t88rcucg1b7uk t758 = gm_l5;
        gm_b16_8 t759 = gm_l25;
        gm_tnpifv20irnph t760;
        GM_ST(gm_b16_8, ((uint8_t *)&t760 + 0), t759);
        gm_tabb710d9mq2e t761;
        gm_f_format_ckgfhm4((void *)&t761, (void *)NULL, cx, NULL, t760);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t761))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t761) + 8, gm_src2j6ljeja7ilp, 166, 38);
        gm_b16_8 t762 = GM_LD(gm_b16_8, (((uint8_t *)&t761) + 8));
        gm_b16_8 t763 = GM_LD(gm_b16_8, ((uint8_t *)&t762));
        gm_b16_8 t764 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t757, t758, t763);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t764);
        goto gm_fdone;
        goto me282;
mn755:;
ma299:;
        uint8_t t766 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t766 != ((uint8_t)26ULL)) goto mn765;
        {
            gm_b16_8 t768;
            void * t770 = gm_l0;
            void * t771 = GM_LD(void *, (((uint8_t *)t770) + 0));
            void * t772 = (void *)(((uint8_t *)t771) + 8);
            gm_t88rcucg1b7uk t773 = gm_l26;
            gm_t53at80pfdus3 t774;
            gm_f_kind_ghvav22((void *)&t774, t772, GM_CONV(gm_csguankko4lmhd, t773));
            uint8_t t775 = (((GM_LD(uint8_t, ((uint8_t *)&t774) + 0) == ((uint8_t)3ULL)) && (GM_LD(uint16_t, (((uint8_t *)&t774) + 8)) == ((uint16_t)32ULL))));
            if (!t775) goto ie769;
            gm_b16_8 t777;
            GM_ST(void *, (uint8_t *)&t777, (void *)(gm_s63o5blu3j46d));
            GM_ST(uintptr_t, (uint8_t *)&t777 + 8, (uintptr_t)(15));
            gm_b16_8 t778 = t777;
            t768 = t778;
            goto ix769;
ie769:;
            gm_b16_8 t780;
            GM_ST(void *, (uint8_t *)&t780, (void *)(gm_s565fgm0tsngr));
            GM_ST(uintptr_t, (uint8_t *)&t780 + 8, (uintptr_t)(14));
            gm_b16_8 t781 = t780;
            t768 = t781;
ix769:;
            gm_l52 = t768;
        }
        {
            void * t782 = gm_l0;
            gm_t88rcucg1b7uk t783 = gm_l5;
            gm_t6j6smj7qoctu t784;
            gm_b16_8 t785 = gm_l52;
            GM_ST(gm_b16_8, (uint8_t *)&t784 + 0, t785);
            gm_b16_8 t786 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t784 + 16, t786);
            gm_tabb710d9mq2e t787;
            gm_f_format_nm3fl8o((void *)&t787, (void *)NULL, cx, NULL, &t784);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t787))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t787) + 8, gm_src2j6ljeja7ilp, 169, 35);
            gm_b16_8 t788 = GM_LD(gm_b16_8, (((uint8_t *)&t787) + 8));
            gm_b16_8 t789 = GM_LD(gm_b16_8, ((uint8_t *)&t788));
            gm_b16_8 t790 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t782, t783, t789);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l53 = t790;
        }
        {
            void * t791 = gm_l0;
            gm_t88rcucg1b7uk t792 = gm_l5;
            gm_b16_8 t793 = gm_l53;
            gm_b16_8 t794 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t791, t792, t793);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t794);
            goto gm_fdone;
        }
        goto me282;
mn765:;
ma300:;
        uint8_t t796 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t796 != ((uint8_t)27ULL)) goto mn795;
        {
            gm_b16_8 t798;
            void * t800 = gm_l0;
            void * t801 = GM_LD(void *, (((uint8_t *)t800) + 0));
            void * t802 = (void *)(((uint8_t *)t801) + 8);
            gm_t88rcucg1b7uk t803 = gm_l26;
            gm_t53at80pfdus3 t804;
            gm_f_kind_ghvav22((void *)&t804, t802, GM_CONV(gm_csguankko4lmhd, t803));
            uint8_t t805 = (((GM_LD(uint8_t, ((uint8_t *)&t804) + 0) == ((uint8_t)3ULL)) && (GM_LD(uint16_t, (((uint8_t *)&t804) + 8)) == ((uint16_t)32ULL))));
            if (!t805) goto ie799;
            gm_b16_8 t807;
            GM_ST(void *, (uint8_t *)&t807, (void *)(gm_svpr02j6er8vd));
            GM_ST(uintptr_t, (uint8_t *)&t807 + 8, (uintptr_t)(14));
            gm_b16_8 t808 = t807;
            t798 = t808;
            goto ix799;
ie799:;
            gm_b16_8 t810;
            GM_ST(void *, (uint8_t *)&t810, (void *)(gm_shhrbleiv8o3j));
            GM_ST(uintptr_t, (uint8_t *)&t810 + 8, (uintptr_t)(13));
            gm_b16_8 t811 = t810;
            t798 = t811;
ix799:;
            gm_l54 = t798;
        }
        {
            void * t812 = gm_l0;
            gm_t88rcucg1b7uk t813 = gm_l5;
            gm_t0uuhaljsf410 t814;
            gm_b16_8 t815 = gm_l54;
            GM_ST(gm_b16_8, (uint8_t *)&t814 + 0, t815);
            gm_b16_8 t816 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t814 + 16, t816);
            uint64_t t817 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t818 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t817 >= (uintptr_t)(t818)) gm_trap_index(t817, t818, gm_src2j6ljeja7ilp, 174, 71);
            gm_b16_8 t819 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t817 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t814 + 32, t819);
            uint64_t t820 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t821 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t820 >= (uintptr_t)(t821)) gm_trap_index(t820, t821, gm_src2j6ljeja7ilp, 174, 86);
            gm_b16_8 t822 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t820 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t814 + 48, t822);
            gm_tabb710d9mq2e t823;
            gm_f_format_261kv0l((void *)&t823, (void *)NULL, cx, NULL, &t814);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t823))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t823) + 8, gm_src2j6ljeja7ilp, 174, 35);
            gm_b16_8 t824 = GM_LD(gm_b16_8, (((uint8_t *)&t823) + 8));
            gm_b16_8 t825 = GM_LD(gm_b16_8, ((uint8_t *)&t824));
            gm_b16_8 t826 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t812, t813, t825);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l55 = t826;
        }
        {
            void * t827 = gm_l0;
            gm_t88rcucg1b7uk t828 = gm_l5;
            gm_b16_8 t829 = gm_l55;
            gm_b16_8 t830 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t827, t828, t829);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t830);
            goto gm_fdone;
        }
        goto me282;
mn795:;
ma301:;
        uint8_t t834 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t834 != ((uint8_t)32ULL)) goto pa833;
        goto po832;
pa833:;
        uint8_t t836 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t836 != ((uint8_t)33ULL)) goto pa835;
        goto po832;
pa835:;
        goto mn831;
po832:;
        void * t837 = gm_l0;
        void * t838 = gm_l1;
        gm_tvdg94bk7aa22 t839 = gm_l2;
        gm_t88rcucg1b7uk t840 = gm_l26;
        gm_b16_8 t841 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
        gm_f_rotate_tpbpt1c((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t837, t838, t839, t840, t841);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me282;
mn831:;
ma302:;
        uint8_t t845 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t845 != ((uint8_t)28ULL)) goto pa844;
        goto po843;
pa844:;
        uint8_t t847 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t847 != ((uint8_t)29ULL)) goto pa846;
        goto po843;
pa846:;
        uint8_t t849 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t849 != ((uint8_t)30ULL)) goto pa848;
        goto po843;
pa848:;
        uint8_t t851 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t851 != ((uint8_t)31ULL)) goto pa850;
        goto po843;
pa850:;
        uint8_t t853 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t853 != ((uint8_t)34ULL)) goto pa852;
        goto po843;
pa852:;
        goto mn842;
po843:;
        void * t854 = gm_l0;
        void * t855 = gm_l1;
        gm_tvdg94bk7aa22 t856 = gm_l2;
        gm_t88rcucg1b7uk t857 = gm_l26;
        gm_b16_8 t858 = gm_l25;
        gm_f_bit_count_77uni44((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t854, t855, t856, t857, t858);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me282;
mn842:;
ma303:;
        uint8_t t860 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t860 != ((uint8_t)21ULL)) goto mn859;
        gm_t9qkh0rsooc8d t861 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
        gm_l56 = t861;
        void * t862 = gm_l0;
        void * t863 = gm_l1;
        gm_t9qkh0rsooc8d t864 = gm_l56;
        gm_t88rcucg1b7uk t865 = gm_l26;
        gm_b16_8 t866 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
        gm_f_pow_s0lbtt4((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t862, t863, t864, t865, t866);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me282;
mn859:;
ma304:;
        uint8_t t868 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t868 != ((uint8_t)60ULL)) goto mn867;
        void * t869 = gm_l0;
        gm_t88rcucg1b7uk t870 = gm_l5;
        gm_b16_8 t871 = gm_l25;
        gm_tnpifv20irnph t872;
        GM_ST(gm_b16_8, ((uint8_t *)&t872 + 0), t871);
        gm_tabb710d9mq2e t873;
        gm_f_format_68bbqsa((void *)&t873, (void *)NULL, cx, NULL, t872);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t873))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t873) + 8, gm_src2j6ljeja7ilp, 180, 38);
        gm_b16_8 t874 = GM_LD(gm_b16_8, (((uint8_t *)&t873) + 8));
        gm_b16_8 t875 = GM_LD(gm_b16_8, ((uint8_t *)&t874));
        gm_b16_8 t876 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t869, t870, t875);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t876);
        goto gm_fdone;
        goto me282;
mn867:;
ma305:;
        uint8_t t880 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t880 != ((uint8_t)64ULL)) goto pa879;
        goto po878;
pa879:;
        uint8_t t882 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t882 != ((uint8_t)65ULL)) goto pa881;
        goto po878;
pa881:;
        uint8_t t884 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t884 != ((uint8_t)66ULL)) goto pa883;
        goto po878;
pa883:;
        uint8_t t886 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t886 != ((uint8_t)67ULL)) goto pa885;
        goto po878;
pa885:;
        uint8_t t888 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t888 != ((uint8_t)68ULL)) goto pa887;
        goto po878;
pa887:;
        uint8_t t890 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t890 != ((uint8_t)70ULL)) goto pa889;
        goto po878;
pa889:;
        uint8_t t892 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t892 != ((uint8_t)71ULL)) goto pa891;
        goto po878;
pa891:;
        goto mn877;
po878:;
        void * t893 = gm_l0;
        void * t894 = gm_l1;
        gm_tvdg94bk7aa22 t895 = gm_l2;
        gm_b16_8 t896 = gm_l3;
        gm_b16_8 t897 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
        gm_f_atomic_plj0t12((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t893, t894, t895, t896, t897);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me282;
mn877:;
ma306:;
        uint8_t t899 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t899 != ((uint8_t)69ULL)) goto mn898;
        {
            void * t901 = gm_l0;
            gm_t88rcucg1b7uk t902 = gm_l26;
            gm_b16_8 t903 = gm_l25;
            gm_b16_8 t904 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t901, t902, t903);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l57 = t904;
        }
        {
            void * t905 = gm_l0;
            gm_b16_8 t906 = gm_l57;
            gm_tnpifv20irnph t907;
            GM_ST(gm_b16_8, ((uint8_t *)&t907 + 0), t906);
            gm_b0_1 t908 = gm_f_linef_di9ea86((void *)NULL, cx, NULL, t905, t907);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t909 = gm_l0;
            gm_b16_8 t910 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t909);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t910);
            goto gm_fdone;
        }
        goto me282;
mn898:;
ma307:;
        uint8_t t912 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t912 != ((uint8_t)35ULL)) goto mn911;
        void * t913 = gm_l0;
        void * t914 = gm_l1;
        gm_b16_8 t915 = gm_l25;
        gm_f_black_box_r6deojc((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t913, t914, t915);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me282;
mn911:;
ma308:;
        uint8_t t917 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t917 != ((uint8_t)62ULL)) goto mn916;
        {
            void * t919 = gm_l0;
            void * t920 = GM_LD(void *, (((uint8_t *)t919) + 0));
            void * t921 = (void *)(((uint8_t *)t920) + 1968);
            void * t922 = gm_l0;
            void * t923 = GM_LD(void *, (((uint8_t *)t922) + 0));
            void * t924 = (void *)(((uint8_t *)t923) + 8);
            gm_t88rcucg1b7uk t925 = gm_l26;
            gm_b16_8 t926 = gm_f_hash_fn_nlk82uj((void *)NULL, cx, NULL, t921, t924, t925);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l58 = t926;
        }
        {
            void * t927 = gm_l0;
            gm_t88rcucg1b7uk t928 = gm_l26;
            gm_b16_8 t929 = gm_l25;
            gm_b16_8 t930 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t927, t928, t929);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l59 = t930;
        }
        {
            void * t931 = gm_l0;
            gm_t88rcucg1b7uk t932 = gm_l5;
            gm_tfeab279c16rr t933;
            gm_b16_8 t934 = gm_l58;
            GM_ST(gm_b16_8, (uint8_t *)&t933 + 0, t934);
            gm_b16_8 t935 = gm_l59;
            GM_ST(gm_b16_8, (uint8_t *)&t933 + 16, t935);
            uint64_t t936 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t937 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t936 >= (uintptr_t)(t937)) gm_trap_index(t936, t937, gm_src2j6ljeja7ilp, 191, 95);
            gm_b16_8 t938 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t936 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t933 + 32, t938);
            gm_tabb710d9mq2e t939;
            gm_f_format_3vgp4uu((void *)&t939, (void *)NULL, cx, NULL, &t933);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t939))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t939) + 8, gm_src2j6ljeja7ilp, 191, 30);
            gm_b16_8 t940 = GM_LD(gm_b16_8, (((uint8_t *)&t939) + 8));
            gm_b16_8 t941 = GM_LD(gm_b16_8, ((uint8_t *)&t940));
            gm_b16_8 t942 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t931, t932, t941);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t942);
            goto gm_fdone;
        }
        goto me282;
mn916:;
ma309:;
        void * t944 = gm_l1;
        gm_tl874t2atnlh3 t945 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t944) + 72));
        gm_b16_8 t946;
        GM_ST(void *, (uint8_t *)&t946, (void *)(gm_sv7lsha1e0i8r));
        GM_ST(uintptr_t, (uint8_t *)&t946 + 8, (uintptr_t)(12));
        gm_t08olq97rscn3 t947;
        gm_f_unsupported_531q51r((void *)&t947, (void *)NULL, cx, NULL, t945, t946);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t947);
        goto gm_fdone;
        goto me282;
mn943:;
ma310:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 84, 5);
me282:;
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
__attribute__((aligned(4))) void gm_f_convert_lqrk2n0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_convert_lqrk2n0 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* src */
    gm_b16_8 gm_l4; /* v */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_t88rcucg1b7uk gm_l6; /* target */
    gm_t88rcucg1b7uk gm_l7; /* x */
    gm_b16_8 gm_l8; /* loc */
    uint8_t gm_l9; /* src_int */
    gm_b16_8 gm_l10; /* src_val */
    gm_t88rcucg1b7uk gm_l11; /* tag */
    gm_b16_8 gm_l12; /* a */
    gm_t88rcucg1b7uk gm_l14; /* u */
    gm_b16_8 gm_l15; /* a */
    gm_tuglhjjmj2g58 gm_l16; /* ii */
    gm_b16_8 gm_l17; /* c */
    uint8_t gm_l18; /* signed */
    uint8_t gm_l19; /* si */
    gm_b16_8 gm_l20; /* fits */
    gm_b16_8 gm_l21; /* wrapped */
    gm_b16_8 gm_l22; /* res */
    gm_b16_8 gm_l23; /* w */
    gm_b16_8 gm_l24; /* some */
    gm_b16_8 gm_l25; /* none */
    gm_b16_8 gm_l26; /* min */
    gm_b16_8 gm_l27; /* max */
    gm_b16_8 gm_l28; /* neg */
    void * gm_l29; /* tys */
    gm_t88rcucg1b7uk gm_l30; /* f64_ty */
    gm_b16_8 gm_l31; /* x */
    gm_b16_8 gm_l32; /* rounded */
    gm_b16_8 gm_l33; /* r */
    gm_b16_8 gm_l34; /* lo */
    gm_b16_8 gm_l35; /* hi */
    gm_b16_8 gm_l36; /* in_range */
    gm_b16_8 gm_l37; /* res */
    gm_b16_8 gm_l38; /* w */
    gm_b16_8 gm_l39; /* some */
    gm_b16_8 gm_l40; /* none */
    gm_b16_8 gm_l41; /* min */
    gm_b16_8 gm_l42; /* max */
    gm_b16_8 gm_l43; /* r */
    gm_tcmkompbl09il gm_l44; /* bl */
    gm_tuglhjjmj2g58 gm_l45; /* ui */
    gm_b16_8 gm_l46; /* w */
    gm_b16_8 gm_l47; /* b */
    GM_STACK_CHECK(&F, 6389u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l5 = t3;
    }
    {
        gm_t88rcucg1b7uk t4 = gm_l5;
        gm_l6 = t4;
    }
    {
        uint8_t t6 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)5ULL)));
        if (!t6) goto ie5;
        {
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
            void * t11 = (void *)(((uint8_t *)t10) + 8);
            gm_t88rcucg1b7uk t12 = gm_l5;
            gm_t53at80pfdus3 t13;
            gm_f_kind_ghvav22((void *)&t13, t11, GM_CONV(gm_csguankko4lmhd, t12));
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
            if (t14 != ((uint8_t)13ULL)) goto ie8;
            gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t13) + 8));
            gm_l7 = t15;
            {
                gm_t88rcucg1b7uk t17 = gm_l7;
                gm_l6 = t17;
            }
            goto ix8;
ie8:;
ix8:;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l1;
        gm_b16_8 t20 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t20;
    }
    {
        gm_l9 = ((uint8_t)0);
    }
    {
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(0));
        gm_l10 = t21;
    }
    {
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
        void * t24 = (void *)(((uint8_t *)t23) + 8);
        gm_t88rcucg1b7uk t25 = gm_l3;
        gm_t5v8n6qrn4rh4 t26;
        gm_f_repr_53quqki((void *)&t26, (void *)NULL, cx, NULL, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        switch (t34) { case ((uint8_t)0ULL): goto ma28; case ((uint8_t)1ULL): goto ma30; case ((uint8_t)2ULL): goto ma29; case ((uint8_t)3ULL): goto ma32; case ((uint8_t)4ULL): goto ma32; case ((uint8_t)5ULL): goto ma32; case ((uint8_t)6ULL): goto ma31; default: goto ma32; }
ma28:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t36 != ((uint8_t)0ULL)) goto mn35;
        {
            gm_l9 = ((uint8_t)1);
        }
        {
            gm_b16_8 t38 = gm_l4;
            gm_l10 = t38;
        }
        goto me27;
mn35:;
ma29:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t40 != ((uint8_t)2ULL)) goto mn39;
        {
            gm_l9 = ((uint8_t)1);
        }
        {
            gm_b16_8 t42 = gm_l4;
            gm_tnpifv20irnph t43;
            GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t42);
            gm_tabb710d9mq2e t44;
            gm_f_format_p1ca3i8((void *)&t44, (void *)NULL, cx, NULL, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_src2j6ljeja7ilp, 523, 23);
            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
            gm_l10 = t46;
        }
        goto me27;
mn39:;
ma30:;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t48 != ((uint8_t)1ULL)) goto mn47;
        {
            gm_b16_8 t50 = gm_l4;
            gm_tnpifv20irnph t51;
            GM_ST(gm_b16_8, ((uint8_t *)&t51 + 0), t50);
            gm_tabb710d9mq2e t52;
            gm_f_format_gp2h96m((void *)&t52, (void *)NULL, cx, NULL, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 8, gm_src2j6ljeja7ilp, 525, 34);
            gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&t52) + 8));
            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
            gm_l10 = t54;
        }
        goto me27;
mn47:;
ma31:;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t56 != ((uint8_t)6ULL)) goto mn55;
        void * t57 = gm_l0;
        void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
        void * t59 = (void *)(((uint8_t *)t58) + 8);
        gm_t88rcucg1b7uk t60 = gm_l3;
        gm_t53at80pfdus3 t61;
        gm_f_kind_ghvav22((void *)&t61, t59, GM_CONV(gm_csguankko4lmhd, t60));
        uint8_t t62;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&t61) + 0));
        if (t65 != ((uint8_t)17ULL)) goto mn64;
        t62 = ((uint8_t)1);
        goto me63;
mn64:;
        t62 = ((uint8_t)0);
        goto me63;
mn66:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 526, 18);
me63:;
        if (!t62) goto mn55;
        void * t67 = gm_l0;
        void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
        void * t69 = (void *)(((uint8_t *)t68) + 8);
        gm_t88rcucg1b7uk t70 = gm_l3;
        void * t71 = gm_f_adt_of_gej99vn(t69, GM_CONV(gm_csguankko4lmhd, t70));
        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t71) + 24) + 0));
        switch (t77) { case ((uint8_t)0ULL): goto ma74; case ((uint8_t)1ULL): goto ma73; case ((uint8_t)2ULL): goto ma75; default: goto ma75; }
ma73:;
        uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t71) + 24) + 0));
        if (t79 != ((uint8_t)1ULL)) goto mn78;
        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t71) + 24) + 8) + 0));
        gm_l11 = t80;
        {
            void * t82 = gm_l0;
            gm_t88rcucg1b7uk t83 = gm_l3;
            gm_b16_8 t84 = gm_l4;
            gm_b16_8 t85 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t82, t83, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t85;
        }
        {
            gm_l9 = ((uint8_t)1);
        }
        {
            void * t86 = gm_l0;
            gm_t88rcucg1b7uk t87 = gm_l11;
            gm_b16_8 t88 = gm_l12;
            gm_b16_8 t89 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t86, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t89;
        }
        goto me72;
mn78:;
ma74:;
        uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)t71) + 24) + 0));
        if (t91 != ((uint8_t)0ULL)) goto mn90;
        gm_tpautr4br7h3l t92 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t71) + 24) + 8));
        F.l13 = t92;
        {
            gm_tkn42u7na3t81 t94 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l13) + 40));
            gm_t88rcucg1b7uk t95;
            if (((((uint8_t *)&t94))[4] != 0)) {
                gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t94) + 0));
                t95 = t96;
            } else {
                void * t97 = gm_l1;
                gm_tl874t2atnlh3 t98 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t97) + 72));
                void * t99 = gm_l0;
                void * t100 = GM_LD(void *, (((uint8_t *)t99) + 0));
                void * t101 = (void *)(((uint8_t *)t100) + 8);
                gm_t88rcucg1b7uk t102 = gm_l3;
                gm_b16_8 t103 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t101, t102);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t104;
                GM_ST(gm_b16_8, ((uint8_t *)&t104 + 0), t103);
                gm_tabb710d9mq2e t105;
                gm_f_format_m0j4lkh((void *)&t105, (void *)NULL, cx, NULL, t104);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 8, gm_src2j6ljeja7ilp, 533, 63);
                gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&t105) + 8));
                gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                gm_t08olq97rscn3 t108;
                gm_f_unsupported_531q51r((void *)&t108, (void *)NULL, cx, NULL, t98, t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t108);
                goto gm_fdone;
            }
            gm_l14 = t95;
        }
        {
            void * t109 = gm_l0;
            gm_t88rcucg1b7uk t110 = gm_l3;
            gm_b16_8 t111 = gm_l4;
            gm_b16_8 t112 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t109, t110, t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t112;
        }
        {
            gm_l9 = ((uint8_t)1);
        }
        {
            void * t113 = gm_l0;
            gm_t88rcucg1b7uk t114 = gm_l14;
            gm_b16_8 t115 = gm_l15;
            gm_b16_8 t116 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t113, t114, t115);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t116;
        }
        goto me72;
mn90:;
ma75:;
        void * t118 = gm_l1;
        gm_tl874t2atnlh3 t119 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t118) + 72));
        void * t120 = gm_l0;
        void * t121 = GM_LD(void *, (((uint8_t *)t120) + 0));
        void * t122 = (void *)(((uint8_t *)t121) + 8);
        gm_t88rcucg1b7uk t123 = gm_l3;
        gm_b16_8 t124 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t122, t123);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t125;
        GM_ST(gm_b16_8, ((uint8_t *)&t125 + 0), t124);
        gm_tabb710d9mq2e t126;
        gm_f_format_m0j4lkh((void *)&t126, (void *)NULL, cx, NULL, t125);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 8, gm_src2j6ljeja7ilp, 538, 46);
        gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&t126) + 8));
        gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
        gm_t08olq97rscn3 t129;
        gm_f_unsupported_531q51r((void *)&t129, (void *)NULL, cx, NULL, t119, t128);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t129);
        goto gm_fdone;
        goto me72;
mn117:;
ma76:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 526, 53);
me72:;
        goto me27;
mn55:;
ma32:;
        void * t131 = gm_l1;
        gm_tl874t2atnlh3 t132 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t131) + 72));
        void * t133 = gm_l0;
        void * t134 = GM_LD(void *, (((uint8_t *)t133) + 0));
        void * t135 = (void *)(((uint8_t *)t134) + 8);
        gm_t88rcucg1b7uk t136 = gm_l3;
        gm_b16_8 t137 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t135, t136);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t138;
        GM_ST(gm_b16_8, ((uint8_t *)&t138 + 0), t137);
        gm_tabb710d9mq2e t139;
        gm_f_format_m0j4lkh((void *)&t139, (void *)NULL, cx, NULL, t138);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 8, gm_src2j6ljeja7ilp, 540, 42);
        gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&t139) + 8));
        gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&t140));
        gm_t08olq97rscn3 t142;
        gm_f_unsupported_531q51r((void *)&t142, (void *)NULL, cx, NULL, t132, t141);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t142);
        goto gm_fdone;
        goto me27;
mn130:;
ma33:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 516, 5);
me27:;
    }
    {
        void * t143 = gm_l0;
        void * t144 = GM_LD(void *, (((uint8_t *)t143) + 0));
        void * t145 = (void *)(((uint8_t *)t144) + 8);
        gm_t88rcucg1b7uk t146 = gm_l6;
        gm_t5v8n6qrn4rh4 t147;
        gm_f_repr_53quqki((void *)&t147, (void *)NULL, cx, NULL, t145, t146);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&t147) + 0));
        switch (t153) { case ((uint8_t)0ULL): goto ma149; case ((uint8_t)1ULL): goto ma150; case ((uint8_t)2ULL): goto ma151; case ((uint8_t)3ULL): goto ma151; case ((uint8_t)4ULL): goto ma151; case ((uint8_t)5ULL): goto ma151; case ((uint8_t)6ULL): goto ma151; default: goto ma151; }
ma149:;
        uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&t147) + 0));
        if (t155 != ((uint8_t)0ULL)) goto mn154;
        {
            void * t157 = gm_l0;
            gm_t88rcucg1b7uk t158 = gm_l6;
            gm_tqcc6dmlne11b t159;
            gm_f_int_info_2tcdf1n((void *)&t159, (void *)NULL, cx, NULL, t157, t158);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t159))[16] != 0)) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 544, 19);
            gm_tuglhjjmj2g58 t160 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t159));
            gm_l16 = t160;
        }
        {
            gm_tuglhjjmj2g58 t161 = gm_l16;
            gm_b16_8 t162 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t161));
            gm_l17 = t162;
        }
        {
            uint8_t t164 = gm_l9;
            if (!t164) goto ie163;
            {
                void * t166 = gm_l0;
                void * t167 = GM_LD(void *, (((uint8_t *)t166) + 0));
                void * t168 = (void *)(((uint8_t *)t167) + 8);
                gm_t88rcucg1b7uk t169 = gm_l3;
                gm_t5v8n6qrn4rh4 t170;
                gm_f_repr_53quqki((void *)&t170, (void *)NULL, cx, NULL, t168, t169);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t171;
                uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&t170) + 0));
                if (t174 != ((uint8_t)0ULL)) goto mn173;
                uint8_t t175 = GM_LD(uint8_t, ((((uint8_t *)&t170) + 8) + 0));
                gm_l18 = t175;
                uint8_t t176 = gm_l18;
                t171 = t176;
                goto me172;
mn173:;
                t171 = ((uint8_t)1);
                goto me172;
mn177:;
                gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 547, 28);
me172:;
                gm_l19 = t171;
            }
            {
                gm_tuglhjjmj2g58 t178 = gm_l16;
                gm_b16_8 t179 = gm_l10;
                uint8_t t180 = gm_l19;
                gm_b16_8 t181 = gm_f_arith_fits_7degt3e((void *)NULL, cx, NULL, t178, t179, t180);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t181;
            }
            {
                gm_tuglhjjmj2g58 t182 = gm_l16;
                gm_t6j6smj7qoctu t183;
                gm_b16_8 t184 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t183 + 0, t184);
                gm_b16_8 t185 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t183 + 16, t185);
                gm_tabb710d9mq2e t186;
                gm_f_format_3vtl3te((void *)&t186, (void *)NULL, cx, NULL, &t183);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t186))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t186) + 8, gm_src2j6ljeja7ilp, 552, 42);
                gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)&t186) + 8));
                gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
                gm_b16_8 t189 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t182, t188);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l21 = t189;
            }
            {
                uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t195) { case ((uint8_t)0ULL): goto ma193; case ((uint8_t)1ULL): goto ma193; case ((uint8_t)2ULL): goto ma193; case ((uint8_t)3ULL): goto ma193; case ((uint8_t)4ULL): goto ma193; case ((uint8_t)5ULL): goto ma191; case ((uint8_t)6ULL): goto ma192; case ((uint8_t)7ULL): goto ma193; case ((uint8_t)8ULL): goto ma193; case ((uint8_t)9ULL): goto ma193; case ((uint8_t)10ULL): goto ma193; case ((uint8_t)11ULL): goto ma193; case ((uint8_t)12ULL): goto ma193; case ((uint8_t)13ULL): goto ma193; case ((uint8_t)14ULL): goto ma193; case ((uint8_t)15ULL): goto ma193; case ((uint8_t)16ULL): goto ma193; case ((uint8_t)17ULL): goto ma193; case ((uint8_t)18ULL): goto ma193; case ((uint8_t)19ULL): goto ma193; case ((uint8_t)20ULL): goto ma193; case ((uint8_t)21ULL): goto ma193; case ((uint8_t)22ULL): goto ma193; case ((uint8_t)23ULL): goto ma193; case ((uint8_t)24ULL): goto ma193; case ((uint8_t)25ULL): goto ma193; case ((uint8_t)26ULL): goto ma193; case ((uint8_t)27ULL): goto ma193; case ((uint8_t)28ULL): goto ma193; case ((uint8_t)29ULL): goto ma193; case ((uint8_t)30ULL): goto ma193; case ((uint8_t)31ULL): goto ma193; case ((uint8_t)32ULL): goto ma193; case ((uint8_t)33ULL): goto ma193; case ((uint8_t)34ULL): goto ma193; case ((uint8_t)35ULL): goto ma193; case ((uint8_t)36ULL): goto ma193; case ((uint8_t)37ULL): goto ma193; case ((uint8_t)38ULL): goto ma193; case ((uint8_t)39ULL): goto ma193; case ((uint8_t)40ULL): goto ma193; case ((uint8_t)41ULL): goto ma193; case ((uint8_t)42ULL): goto ma193; case ((uint8_t)43ULL): goto ma193; case ((uint8_t)44ULL): goto ma193; case ((uint8_t)45ULL): goto ma193; case ((uint8_t)46ULL): goto ma193; case ((uint8_t)47ULL): goto ma193; case ((uint8_t)48ULL): goto ma193; case ((uint8_t)49ULL): goto ma193; case ((uint8_t)50ULL): goto ma193; case ((uint8_t)51ULL): goto ma193; case ((uint8_t)52ULL): goto ma193; case ((uint8_t)53ULL): goto ma193; case ((uint8_t)54ULL): goto ma193; case ((uint8_t)55ULL): goto ma193; case ((uint8_t)56ULL): goto ma193; case ((uint8_t)57ULL): goto ma193; case ((uint8_t)58ULL): goto ma193; case ((uint8_t)59ULL): goto ma193; case ((uint8_t)60ULL): goto ma193; case ((uint8_t)61ULL): goto ma193; case ((uint8_t)62ULL): goto ma193; case ((uint8_t)63ULL): goto ma193; case ((uint8_t)64ULL): goto ma193; case ((uint8_t)65ULL): goto ma193; case ((uint8_t)66ULL): goto ma193; case ((uint8_t)67ULL): goto ma193; case ((uint8_t)68ULL): goto ma193; case ((uint8_t)69ULL): goto ma193; case ((uint8_t)70ULL): goto ma193; case ((uint8_t)71ULL): goto ma193; case ((uint8_t)72ULL): goto ma193; case ((uint8_t)73ULL): goto ma193; case ((uint8_t)74ULL): goto ma193; case ((uint8_t)75ULL): goto ma193; case ((uint8_t)76ULL): goto ma193; case ((uint8_t)77ULL): goto ma193; case ((uint8_t)78ULL): goto ma193; case ((uint8_t)79ULL): goto ma193; case ((uint8_t)80ULL): goto ma193; case ((uint8_t)81ULL): goto ma193; default: goto ma193; }
ma191:;
                uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t197 != ((uint8_t)5ULL)) goto mn196;
                {
                    void * t199 = gm_l0;
                    gm_t88rcucg1b7uk t200 = gm_l5;
                    gm_b16_8 t201 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t199, t200);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l22 = t201;
                }
                {
                    void * t202 = gm_l0;
                    gm_t88rcucg1b7uk t203 = gm_l6;
                    gm_b16_8 t204 = gm_l21;
                    gm_b16_8 t205 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t202, t203, t204);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l23 = t205;
                }
                {
                    void * t206 = gm_l0;
                    gm_t88rcucg1b7uk t207 = gm_l5;
                    gm_b16_8 t208 = gm_l23;
                    gm_b16_8 t209 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t206, t207, t208);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l24 = t209;
                }
                {
                    void * t210 = gm_l0;
                    gm_t88rcucg1b7uk t211 = gm_l5;
                    gm_b16_8 t212 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t210, t211);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l25 = t212;
                }
                {
                    void * t213 = gm_l0;
                    gm_t0uuhaljsf410 t214;
                    gm_b16_8 t215 = gm_l22;
                    GM_ST(gm_b16_8, (uint8_t *)&t214 + 0, t215);
                    gm_b16_8 t216 = gm_l20;
                    GM_ST(gm_b16_8, (uint8_t *)&t214 + 16, t216);
                    gm_b16_8 t217 = gm_l24;
                    GM_ST(gm_b16_8, (uint8_t *)&t214 + 32, t217);
                    gm_b16_8 t218 = gm_l25;
                    GM_ST(gm_b16_8, (uint8_t *)&t214 + 48, t218);
                    gm_b0_1 t219 = gm_f_linef_6h7r936((void *)NULL, cx, NULL, t213, &t214);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b16_8 t220 = gm_l22;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t220);
                    goto gm_fdone;
                }
                goto me190;
mn196:;
ma192:;
                uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t222 != ((uint8_t)6ULL)) goto mn221;
                {
                    void * t224 = gm_l0;
                    gm_t88rcucg1b7uk t225 = gm_l6;
                    gm_tuglhjjmj2g58 t226 = gm_l16;
                    gm_tbci5ospk5nnn t227;
                    gm_f_ii_min_lku7pdi((void *)&t227, (void *)NULL, cx, NULL, t226);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t228 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t224, t225, &t227);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l26 = t228;
                }
                {
                    void * t229 = gm_l0;
                    gm_t88rcucg1b7uk t230 = gm_l6;
                    gm_tuglhjjmj2g58 t231 = gm_l16;
                    gm_tbci5ospk5nnn t232;
                    gm_f_ii_max_gtp5h90((void *)&t232, (void *)NULL, cx, NULL, t231);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t233 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t229, t230, &t232);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l27 = t233;
                }
                {
                    gm_b16_8 t234;
                    uint8_t t236 = gm_l19;
                    if (!t236) goto ie235;
                    gm_b16_8 t238 = gm_l10;
                    gm_tnpifv20irnph t239;
                    GM_ST(gm_b16_8, ((uint8_t *)&t239 + 0), t238);
                    gm_tabb710d9mq2e t240;
                    gm_f_format_uhjjs86((void *)&t240, (void *)NULL, cx, NULL, t239);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t240))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t240) + 8, gm_src2j6ljeja7ilp, 565, 47);
                    gm_b16_8 t241 = GM_LD(gm_b16_8, (((uint8_t *)&t240) + 8));
                    gm_b16_8 t242 = t241;
                    gm_b16_8 t243 = GM_LD(gm_b16_8, ((uint8_t *)&t242));
                    t234 = t243;
                    goto ix235;
ie235:;
                    gm_b16_8 t245;
                    GM_ST(void *, (uint8_t *)&t245, (void *)(gm_sicgmpca63jln));
                    GM_ST(uintptr_t, (uint8_t *)&t245 + 8, (uintptr_t)(1));
                    gm_b16_8 t246 = t245;
                    t234 = t246;
ix235:;
                    gm_l28 = t234;
                }
                {
                    void * t247 = gm_l0;
                    gm_t88rcucg1b7uk t248 = gm_l5;
                    gm_tud7oovfvnf43 t249;
                    gm_b16_8 t250 = gm_l20;
                    GM_ST(gm_b16_8, (uint8_t *)&t249 + 0, t250);
                    gm_b16_8 t251 = gm_l21;
                    GM_ST(gm_b16_8, (uint8_t *)&t249 + 16, t251);
                    gm_b16_8 t252 = gm_l28;
                    GM_ST(gm_b16_8, (uint8_t *)&t249 + 32, t252);
                    gm_b16_8 t253 = gm_l26;
                    GM_ST(gm_b16_8, (uint8_t *)&t249 + 48, t253);
                    gm_b16_8 t254 = gm_l27;
                    GM_ST(gm_b16_8, (uint8_t *)&t249 + 64, t254);
                    gm_tabb710d9mq2e t255;
                    gm_f_format_nkjq1if((void *)&t255, (void *)NULL, cx, NULL, &t249);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t255))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t255) + 8, gm_src2j6ljeja7ilp, 566, 42);
                    gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)&t255) + 8));
                    gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&t256));
                    gm_b16_8 t258 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t247, t248, t257);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t258);
                    goto gm_fdone;
                }
                goto me190;
mn221:;
ma193:;
                void * t260 = gm_l0;
                gm_t88rcucg1b7uk t261 = gm_l5;
                gm_b16_8 t262 = gm_l21;
                gm_b16_8 t263 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t260, t261, t262);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t263);
                goto gm_fdone;
                goto me190;
mn259:;
ma194:;
                gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 553, 17);
me190:;
            }
            goto ix163;
ie163:;
ix163:;
        }
        {
            void * t264 = gm_l0;
            void * t265 = GM_LD(void *, (((uint8_t *)t264) + 0));
            void * t266 = GM_LD(void *, ((((uint8_t *)t265) + 8) + 0));
            void * t267 = (void *)(((uint8_t *)t266) + 8);
            gm_l29 = t267;
        }
        {
            void * t268 = gm_l29;
            gm_t53at80pfdus3 t269;
            memset(&t269, 0, sizeof t269);
            GM_ST(uint8_t, ((uint8_t *)&t269 + 0), ((uint8_t)3ULL));
            GM_ST(uint16_t, ((uint8_t *)&t269 + 8), ((uint16_t)64ULL));
            gm_t88rcucg1b7uk t270 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t268, &t269);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t270;
        }
        {
            void * t271 = gm_l0;
            gm_t88rcucg1b7uk t272 = gm_l30;
            gm_b16_8 t273 = gm_l10;
            gm_b16_8 t274 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t271, t272, t273);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l31 = t274;
        }
        {
            gm_b16_8 t275;
            uint8_t t282 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t282) { case ((uint8_t)0ULL): goto ma280; case ((uint8_t)1ULL): goto ma280; case ((uint8_t)2ULL): goto ma277; case ((uint8_t)3ULL): goto ma278; case ((uint8_t)4ULL): goto ma279; case ((uint8_t)5ULL): goto ma280; case ((uint8_t)6ULL): goto ma280; case ((uint8_t)7ULL): goto ma280; case ((uint8_t)8ULL): goto ma280; case ((uint8_t)9ULL): goto ma280; case ((uint8_t)10ULL): goto ma280; case ((uint8_t)11ULL): goto ma280; case ((uint8_t)12ULL): goto ma280; case ((uint8_t)13ULL): goto ma280; case ((uint8_t)14ULL): goto ma280; case ((uint8_t)15ULL): goto ma280; case ((uint8_t)16ULL): goto ma280; case ((uint8_t)17ULL): goto ma280; case ((uint8_t)18ULL): goto ma280; case ((uint8_t)19ULL): goto ma280; case ((uint8_t)20ULL): goto ma280; case ((uint8_t)21ULL): goto ma280; case ((uint8_t)22ULL): goto ma280; case ((uint8_t)23ULL): goto ma280; case ((uint8_t)24ULL): goto ma280; case ((uint8_t)25ULL): goto ma280; case ((uint8_t)26ULL): goto ma280; case ((uint8_t)27ULL): goto ma280; case ((uint8_t)28ULL): goto ma280; case ((uint8_t)29ULL): goto ma280; case ((uint8_t)30ULL): goto ma280; case ((uint8_t)31ULL): goto ma280; case ((uint8_t)32ULL): goto ma280; case ((uint8_t)33ULL): goto ma280; case ((uint8_t)34ULL): goto ma280; case ((uint8_t)35ULL): goto ma280; case ((uint8_t)36ULL): goto ma280; case ((uint8_t)37ULL): goto ma280; case ((uint8_t)38ULL): goto ma280; case ((uint8_t)39ULL): goto ma280; case ((uint8_t)40ULL): goto ma280; case ((uint8_t)41ULL): goto ma280; case ((uint8_t)42ULL): goto ma280; case ((uint8_t)43ULL): goto ma280; case ((uint8_t)44ULL): goto ma280; case ((uint8_t)45ULL): goto ma280; case ((uint8_t)46ULL): goto ma280; case ((uint8_t)47ULL): goto ma280; case ((uint8_t)48ULL): goto ma280; case ((uint8_t)49ULL): goto ma280; case ((uint8_t)50ULL): goto ma280; case ((uint8_t)51ULL): goto ma280; case ((uint8_t)52ULL): goto ma280; case ((uint8_t)53ULL): goto ma280; case ((uint8_t)54ULL): goto ma280; case ((uint8_t)55ULL): goto ma280; case ((uint8_t)56ULL): goto ma280; case ((uint8_t)57ULL): goto ma280; case ((uint8_t)58ULL): goto ma280; case ((uint8_t)59ULL): goto ma280; case ((uint8_t)60ULL): goto ma280; case ((uint8_t)61ULL): goto ma280; case ((uint8_t)62ULL): goto ma280; case ((uint8_t)63ULL): goto ma280; case ((uint8_t)64ULL): goto ma280; case ((uint8_t)65ULL): goto ma280; case ((uint8_t)66ULL): goto ma280; case ((uint8_t)67ULL): goto ma280; case ((uint8_t)68ULL): goto ma280; case ((uint8_t)69ULL): goto ma280; case ((uint8_t)70ULL): goto ma280; case ((uint8_t)71ULL): goto ma280; case ((uint8_t)72ULL): goto ma280; case ((uint8_t)73ULL): goto ma280; case ((uint8_t)74ULL): goto ma280; case ((uint8_t)75ULL): goto ma280; case ((uint8_t)76ULL): goto ma280; case ((uint8_t)77ULL): goto ma280; case ((uint8_t)78ULL): goto ma280; case ((uint8_t)79ULL): goto ma280; case ((uint8_t)80ULL): goto ma280; case ((uint8_t)81ULL): goto ma280; default: goto ma280; }
ma277:;
            uint8_t t284 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t284 != ((uint8_t)2ULL)) goto mn283;
            gm_b16_8 t285 = gm_l31;
            gm_tnpifv20irnph t286;
            GM_ST(gm_b16_8, ((uint8_t *)&t286 + 0), t285);
            gm_tabb710d9mq2e t287;
            gm_f_format_3s3fqnk((void *)&t287, (void *)NULL, cx, NULL, t286);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t287))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t287) + 8, gm_src2j6ljeja7ilp, 575, 27);
            gm_b16_8 t288 = GM_LD(gm_b16_8, (((uint8_t *)&t287) + 8));
            gm_b16_8 t289 = GM_LD(gm_b16_8, ((uint8_t *)&t288));
            t275 = t289;
            goto me276;
mn283:;
ma278:;
            uint8_t t291 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t291 != ((uint8_t)3ULL)) goto mn290;
            gm_b16_8 t292 = gm_l31;
            gm_tnpifv20irnph t293;
            GM_ST(gm_b16_8, ((uint8_t *)&t293 + 0), t292);
            gm_tabb710d9mq2e t294;
            gm_f_format_b05vb48((void *)&t294, (void *)NULL, cx, NULL, t293);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t294))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t294) + 8, gm_src2j6ljeja7ilp, 576, 27);
            gm_b16_8 t295 = GM_LD(gm_b16_8, (((uint8_t *)&t294) + 8));
            gm_b16_8 t296 = GM_LD(gm_b16_8, ((uint8_t *)&t295));
            t275 = t296;
            goto me276;
mn290:;
ma279:;
            uint8_t t298 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t298 != ((uint8_t)4ULL)) goto mn297;
            gm_b16_8 t299 = gm_l31;
            gm_tnpifv20irnph t300;
            GM_ST(gm_b16_8, ((uint8_t *)&t300 + 0), t299);
            gm_tabb710d9mq2e t301;
            gm_f_format_2qdci62((void *)&t301, (void *)NULL, cx, NULL, t300);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t301))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t301) + 8, gm_src2j6ljeja7ilp, 577, 26);
            gm_b16_8 t302 = GM_LD(gm_b16_8, (((uint8_t *)&t301) + 8));
            gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&t302));
            t275 = t303;
            goto me276;
mn297:;
ma280:;
            gm_b16_8 t305 = gm_l31;
            gm_tnpifv20irnph t306;
            GM_ST(gm_b16_8, ((uint8_t *)&t306 + 0), t305);
            gm_tabb710d9mq2e t307;
            gm_f_format_7gtji42((void *)&t307, (void *)NULL, cx, NULL, t306);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t307))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t307) + 8, gm_src2j6ljeja7ilp, 578, 22);
            gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&t307) + 8));
            gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
            t275 = t309;
            goto me276;
mn304:;
ma281:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 574, 31);
me276:;
            gm_l32 = t275;
        }
        {
            void * t310 = gm_l0;
            gm_t88rcucg1b7uk t311 = gm_l30;
            gm_b16_8 t312 = gm_l32;
            gm_b16_8 t313 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t310, t311, t312);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l33 = t313;
        }
        {
            gm_tuglhjjmj2g58 t314 = gm_l16;
            gm_tbci5ospk5nnn t315;
            gm_f_ii_min_lku7pdi((void *)&t315, (void *)NULL, cx, NULL, t314);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            double t316 = gm_f_arith_int_to_f64_tiud1hu(&t315);
            gm_tidu2er0d7h95 t317;
            GM_ST(double, ((uint8_t *)&t317 + 0), t316);
            gm_tabb710d9mq2e t318;
            gm_f_format_eucvbnh((void *)&t318, (void *)NULL, cx, NULL, t317);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t318))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t318) + 8, gm_src2j6ljeja7ilp, 581, 19);
            gm_b16_8 t319 = GM_LD(gm_b16_8, (((uint8_t *)&t318) + 8));
            gm_l34 = t319;
        }
        {
            gm_tuglhjjmj2g58 t320 = gm_l16;
            gm_tbci5ospk5nnn t321;
            gm_f_ii_max_gtp5h90((void *)&t321, (void *)NULL, cx, NULL, t320);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t322;
            gm_f_one_08qg1op((void *)&t322, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t323;
            gm_f_add_420lj6o((void *)&t323, (void *)NULL, cx, NULL, &t321, &t322);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            double t324 = gm_f_arith_int_to_f64_tiud1hu(&t323);
            gm_tidu2er0d7h95 t325;
            GM_ST(double, ((uint8_t *)&t325 + 0), t324);
            gm_tabb710d9mq2e t326;
            gm_f_format_eucvbnh((void *)&t326, (void *)NULL, cx, NULL, t325);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t326))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t326) + 8, gm_src2j6ljeja7ilp, 582, 19);
            gm_b16_8 t327 = GM_LD(gm_b16_8, (((uint8_t *)&t326) + 8));
            gm_l35 = t327;
        }
        {
            gm_tud7oovfvnf43 t328;
            gm_b16_8 t329 = gm_l33;
            GM_ST(gm_b16_8, (uint8_t *)&t328 + 0, t329);
            gm_b16_8 t330 = gm_l33;
            GM_ST(gm_b16_8, (uint8_t *)&t328 + 16, t330);
            gm_b16_8 t331 = gm_l34;
            GM_ST(gm_b16_8, (uint8_t *)&t328 + 32, t331);
            gm_b16_8 t332 = gm_l33;
            GM_ST(gm_b16_8, (uint8_t *)&t328 + 48, t332);
            gm_b16_8 t333 = gm_l35;
            GM_ST(gm_b16_8, (uint8_t *)&t328 + 64, t333);
            gm_tabb710d9mq2e t334;
            gm_f_format_aculdom((void *)&t334, (void *)NULL, cx, NULL, &t328);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t334))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t334) + 8, gm_src2j6ljeja7ilp, 583, 25);
            gm_b16_8 t335 = GM_LD(gm_b16_8, (((uint8_t *)&t334) + 8));
            gm_l36 = t335;
        }
        {
            uint8_t t341 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t341) { case ((uint8_t)0ULL): goto ma339; case ((uint8_t)1ULL): goto ma339; case ((uint8_t)2ULL): goto ma339; case ((uint8_t)3ULL): goto ma339; case ((uint8_t)4ULL): goto ma339; case ((uint8_t)5ULL): goto ma337; case ((uint8_t)6ULL): goto ma338; case ((uint8_t)7ULL): goto ma339; case ((uint8_t)8ULL): goto ma339; case ((uint8_t)9ULL): goto ma339; case ((uint8_t)10ULL): goto ma339; case ((uint8_t)11ULL): goto ma339; case ((uint8_t)12ULL): goto ma339; case ((uint8_t)13ULL): goto ma339; case ((uint8_t)14ULL): goto ma339; case ((uint8_t)15ULL): goto ma339; case ((uint8_t)16ULL): goto ma339; case ((uint8_t)17ULL): goto ma339; case ((uint8_t)18ULL): goto ma339; case ((uint8_t)19ULL): goto ma339; case ((uint8_t)20ULL): goto ma339; case ((uint8_t)21ULL): goto ma339; case ((uint8_t)22ULL): goto ma339; case ((uint8_t)23ULL): goto ma339; case ((uint8_t)24ULL): goto ma339; case ((uint8_t)25ULL): goto ma339; case ((uint8_t)26ULL): goto ma339; case ((uint8_t)27ULL): goto ma339; case ((uint8_t)28ULL): goto ma339; case ((uint8_t)29ULL): goto ma339; case ((uint8_t)30ULL): goto ma339; case ((uint8_t)31ULL): goto ma339; case ((uint8_t)32ULL): goto ma339; case ((uint8_t)33ULL): goto ma339; case ((uint8_t)34ULL): goto ma339; case ((uint8_t)35ULL): goto ma339; case ((uint8_t)36ULL): goto ma339; case ((uint8_t)37ULL): goto ma339; case ((uint8_t)38ULL): goto ma339; case ((uint8_t)39ULL): goto ma339; case ((uint8_t)40ULL): goto ma339; case ((uint8_t)41ULL): goto ma339; case ((uint8_t)42ULL): goto ma339; case ((uint8_t)43ULL): goto ma339; case ((uint8_t)44ULL): goto ma339; case ((uint8_t)45ULL): goto ma339; case ((uint8_t)46ULL): goto ma339; case ((uint8_t)47ULL): goto ma339; case ((uint8_t)48ULL): goto ma339; case ((uint8_t)49ULL): goto ma339; case ((uint8_t)50ULL): goto ma339; case ((uint8_t)51ULL): goto ma339; case ((uint8_t)52ULL): goto ma339; case ((uint8_t)53ULL): goto ma339; case ((uint8_t)54ULL): goto ma339; case ((uint8_t)55ULL): goto ma339; case ((uint8_t)56ULL): goto ma339; case ((uint8_t)57ULL): goto ma339; case ((uint8_t)58ULL): goto ma339; case ((uint8_t)59ULL): goto ma339; case ((uint8_t)60ULL): goto ma339; case ((uint8_t)61ULL): goto ma339; case ((uint8_t)62ULL): goto ma339; case ((uint8_t)63ULL): goto ma339; case ((uint8_t)64ULL): goto ma339; case ((uint8_t)65ULL): goto ma339; case ((uint8_t)66ULL): goto ma339; case ((uint8_t)67ULL): goto ma339; case ((uint8_t)68ULL): goto ma339; case ((uint8_t)69ULL): goto ma339; case ((uint8_t)70ULL): goto ma339; case ((uint8_t)71ULL): goto ma339; case ((uint8_t)72ULL): goto ma339; case ((uint8_t)73ULL): goto ma339; case ((uint8_t)74ULL): goto ma339; case ((uint8_t)75ULL): goto ma339; case ((uint8_t)76ULL): goto ma339; case ((uint8_t)77ULL): goto ma339; case ((uint8_t)78ULL): goto ma339; case ((uint8_t)79ULL): goto ma339; case ((uint8_t)80ULL): goto ma339; case ((uint8_t)81ULL): goto ma339; default: goto ma339; }
ma337:;
            uint8_t t343 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t343 != ((uint8_t)5ULL)) goto mn342;
            {
                void * t345 = gm_l0;
                gm_t88rcucg1b7uk t346 = gm_l5;
                gm_b16_8 t347 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t345, t346);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l37 = t347;
            }
            {
                void * t348 = gm_l0;
                gm_tfeab279c16rr t349;
                gm_b16_8 t350 = gm_l36;
                GM_ST(gm_b16_8, (uint8_t *)&t349 + 0, t350);
                gm_b16_8 t351 = gm_l31;
                GM_ST(gm_b16_8, (uint8_t *)&t349 + 16, t351);
                gm_b16_8 t352 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t349 + 32, t352);
                gm_b0_1 t353 = gm_f_linef_6sel63v((void *)NULL, cx, NULL, t348, &t349);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t354 = gm_l0;
                gm_t88rcucg1b7uk t355 = gm_l6;
                gm_t6j6smj7qoctu t356;
                gm_b16_8 t357 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t356 + 0, t357);
                gm_b16_8 t358 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t356 + 16, t358);
                gm_tabb710d9mq2e t359;
                gm_f_format_3vtl3te((void *)&t359, (void *)NULL, cx, NULL, &t356);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t359))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t359) + 8, gm_src2j6ljeja7ilp, 588, 41);
                gm_b16_8 t360 = GM_LD(gm_b16_8, (((uint8_t *)&t359) + 8));
                gm_b16_8 t361 = GM_LD(gm_b16_8, ((uint8_t *)&t360));
                gm_b16_8 t362 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t354, t355, t361);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l38 = t362;
            }
            {
                void * t363 = gm_l0;
                gm_t88rcucg1b7uk t364 = gm_l5;
                gm_b16_8 t365 = gm_l38;
                gm_b16_8 t366 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t363, t364, t365);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l39 = t366;
            }
            {
                void * t367 = gm_l0;
                gm_t6j6smj7qoctu t368;
                gm_b16_8 t369 = gm_l37;
                GM_ST(gm_b16_8, (uint8_t *)&t368 + 0, t369);
                gm_b16_8 t370 = gm_l39;
                GM_ST(gm_b16_8, (uint8_t *)&t368 + 16, t370);
                gm_b0_1 t371 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t367, &t368);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t372 = gm_l0;
                gm_b16_8 t373;
                GM_ST(void *, (uint8_t *)&t373, (void *)(gm_s02uuedsru1qu));
                GM_ST(uintptr_t, (uint8_t *)&t373 + 8, (uintptr_t)(8));
                gm_b0_1 t374 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t372, t373);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t375 = gm_l0;
                gm_t88rcucg1b7uk t376 = gm_l5;
                gm_b16_8 t377 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t375, t376);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l40 = t377;
            }
            {
                void * t378 = gm_l0;
                gm_t6j6smj7qoctu t379;
                gm_b16_8 t380 = gm_l37;
                GM_ST(gm_b16_8, (uint8_t *)&t379 + 0, t380);
                gm_b16_8 t381 = gm_l40;
                GM_ST(gm_b16_8, (uint8_t *)&t379 + 16, t381);
                gm_b0_1 t382 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t378, &t379);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t383 = gm_l0;
                gm_b16_8 t384;
                GM_ST(void *, (uint8_t *)&t384, (void *)(gm_s2cuflhqkkuaq));
                GM_ST(uintptr_t, (uint8_t *)&t384 + 8, (uintptr_t)(1));
                gm_b0_1 t385 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t383, t384);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t386 = gm_l37;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t386);
                goto gm_fdone;
            }
            goto me336;
mn342:;
ma338:;
            uint8_t t388 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t388 != ((uint8_t)6ULL)) goto mn387;
            {
                void * t390 = gm_l0;
                gm_t88rcucg1b7uk t391 = gm_l6;
                gm_tuglhjjmj2g58 t392 = gm_l16;
                gm_tbci5ospk5nnn t393;
                gm_f_ii_min_lku7pdi((void *)&t393, (void *)NULL, cx, NULL, t392);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t394 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t390, t391, &t393);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l41 = t394;
            }
            {
                void * t395 = gm_l0;
                gm_t88rcucg1b7uk t396 = gm_l6;
                gm_tuglhjjmj2g58 t397 = gm_l16;
                gm_tbci5ospk5nnn t398;
                gm_f_ii_max_gtp5h90((void *)&t398, (void *)NULL, cx, NULL, t397);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t399 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t395, t396, &t398);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l42 = t399;
            }
            {
                void * t400 = gm_l0;
                gm_t88rcucg1b7uk t401 = gm_l5;
                gm_tk6rvhh54g86p t402;
                gm_b16_8 t403 = gm_l31;
                GM_ST(gm_b16_8, (uint8_t *)&t402 + 0, t403);
                gm_b16_8 t404 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t402 + 16, t404);
                gm_b16_8 t405 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t402 + 32, t405);
                gm_b16_8 t406 = gm_l34;
                GM_ST(gm_b16_8, (uint8_t *)&t402 + 48, t406);
                gm_b16_8 t407 = gm_l41;
                GM_ST(gm_b16_8, (uint8_t *)&t402 + 64, t407);
                gm_b16_8 t408 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t402 + 80, t408);
                gm_b16_8 t409 = gm_l35;
                GM_ST(gm_b16_8, (uint8_t *)&t402 + 96, t409);
                gm_b16_8 t410 = gm_l42;
                GM_ST(gm_b16_8, (uint8_t *)&t402 + 112, t410);
                gm_b16_8 t411 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t402 + 128, t411);
                gm_b16_8 t412 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t402 + 144, t412);
                gm_tabb710d9mq2e t413;
                gm_f_format_fh9to9u((void *)&t413, (void *)NULL, cx, NULL, &t402);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t413))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t413) + 8, gm_src2j6ljeja7ilp, 600, 38);
                gm_b16_8 t414 = GM_LD(gm_b16_8, (((uint8_t *)&t413) + 8));
                gm_b16_8 t415 = GM_LD(gm_b16_8, ((uint8_t *)&t414));
                gm_b16_8 t416 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t400, t401, t415);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t416);
                goto gm_fdone;
            }
            goto me336;
mn387:;
ma339:;
            {
                void * t420 = gm_l0;
                void * t421 = GM_LD(void *, (((uint8_t *)t420) + 0));
                uint8_t t422 = GM_LD(uint8_t, ((((uint8_t *)t421) + 1136) + 0));
                if (!t422) goto ie419;
                {
                    void * t424 = gm_l0;
                    gm_t6j6smj7qoctu t425;
                    gm_b16_8 t426 = gm_l36;
                    GM_ST(gm_b16_8, (uint8_t *)&t425 + 0, t426);
                    gm_b16_8 t427 = gm_l8;
                    GM_ST(gm_b16_8, (uint8_t *)&t425 + 16, t427);
                    gm_b0_1 t428 = gm_f_linef_jl8o1sr((void *)NULL, cx, NULL, t424, &t425);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix419;
ie419:;
ix419:;
            }
            {
                void * t429 = gm_l0;
                gm_t88rcucg1b7uk t430 = gm_l5;
                gm_t6j6smj7qoctu t431;
                gm_b16_8 t432 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t431 + 0, t432);
                gm_b16_8 t433 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t431 + 16, t433);
                gm_tabb710d9mq2e t434;
                gm_f_format_3vtl3te((void *)&t434, (void *)NULL, cx, NULL, &t431);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t434))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t434) + 8, gm_src2j6ljeja7ilp, 606, 38);
                gm_b16_8 t435 = GM_LD(gm_b16_8, (((uint8_t *)&t434) + 8));
                gm_b16_8 t436 = GM_LD(gm_b16_8, ((uint8_t *)&t435));
                gm_b16_8 t437 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t429, t430, t436);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t437);
                goto gm_fdone;
            }
            goto me336;
mn417:;
ma340:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 584, 13);
me336:;
        }
        goto me148;
mn154:;
ma150:;
        uint8_t t439 = GM_LD(uint8_t, (((uint8_t *)&t147) + 0));
        if (t439 != ((uint8_t)1ULL)) goto mn438;
        {
            void * t441 = gm_l0;
            gm_t88rcucg1b7uk t442 = gm_l6;
            gm_b16_8 t443 = gm_l10;
            gm_b16_8 t444 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t441, t442, t443);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l43 = t444;
        }
        {
            void * t445 = gm_l0;
            gm_t88rcucg1b7uk t446 = gm_l5;
            gm_b16_8 t447 = gm_l43;
            gm_b16_8 t448 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t445, t446, t447);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t448);
            goto gm_fdone;
        }
        goto me148;
mn438:;
ma151:;
        goto me148;
mn449:;
ma152:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 542, 5);
me148:;
    }
    {
        uint8_t t452 = gm_l9;
        if (!t452) goto ie451;
        {
            void * t455 = gm_l0;
            void * t456 = GM_LD(void *, (((uint8_t *)t455) + 0));
            void * t457 = (void *)(((uint8_t *)t456) + 8);
            gm_t88rcucg1b7uk t458 = gm_l6;
            gm_t57euh612ligr t459;
            gm_f_bits_layout_23rect9((void *)&t459, (void *)NULL, cx, NULL, t457, t458);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t459))[24] != 0)) goto ie454;
            gm_l44 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t459) + 0));
            {
                void * t461 = gm_l0;
                gm_t88rcucg1b7uk t462 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l44) + 0));
                gm_tqcc6dmlne11b t463;
                gm_f_int_info_2tcdf1n((void *)&t463, (void *)NULL, cx, NULL, t461, t462);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t463))[16] != 0)) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 619, 19);
                gm_tuglhjjmj2g58 t464 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t463));
                gm_l45 = t464;
            }
            {
                gm_tuglhjjmj2g58 t465 = gm_l45;
                gm_t6j6smj7qoctu t466;
                gm_tuglhjjmj2g58 t467 = gm_l45;
                gm_b16_8 t468 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t467));
                GM_ST(gm_b16_8, (uint8_t *)&t466 + 0, t468);
                gm_b16_8 t469 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t466 + 16, t469);
                gm_tabb710d9mq2e t470;
                gm_f_format_3vtl3te((void *)&t470, (void *)NULL, cx, NULL, &t466);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t470))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t470) + 8, gm_src2j6ljeja7ilp, 620, 32);
                gm_b16_8 t471 = GM_LD(gm_b16_8, (((uint8_t *)&t470) + 8));
                gm_b16_8 t472 = GM_LD(gm_b16_8, ((uint8_t *)&t471));
                gm_b16_8 t473 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t465, t472);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l46 = t473;
            }
            {
                void * t474 = gm_l0;
                gm_t88rcucg1b7uk t475 = gm_l6;
                gm_b16_8 t476 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t474, t475);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l47 = t476;
            }
            {
                void * t477 = gm_l0;
                gm_tfeab279c16rr t478;
                gm_tuglhjjmj2g58 t479 = gm_l45;
                gm_b16_8 t480 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t479));
                GM_ST(gm_b16_8, (uint8_t *)&t478 + 0, t480);
                gm_b16_8 t481 = gm_l47;
                GM_ST(gm_b16_8, (uint8_t *)&t478 + 16, t481);
                gm_b16_8 t482 = gm_l46;
                GM_ST(gm_b16_8, (uint8_t *)&t478 + 32, t482);
                gm_b0_1 t483 = gm_f_linef_21h063v((void *)NULL, cx, NULL, t477, &t478);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t484 = gm_l47;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t484);
                goto gm_fdone;
            }
            goto ix454;
ie454:;
ix454:;
        }
        goto ix451;
ie451:;
ix451:;
    }
    {
        void * t485 = gm_l1;
        gm_tl874t2atnlh3 t486 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t485) + 72));
        void * t487 = gm_l0;
        void * t488 = GM_LD(void *, (((uint8_t *)t487) + 0));
        void * t489 = (void *)(((uint8_t *)t488) + 8);
        gm_t88rcucg1b7uk t490 = gm_l6;
        gm_b16_8 t491 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t489, t490);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t492;
        GM_ST(gm_b16_8, ((uint8_t *)&t492 + 0), t491);
        gm_tabb710d9mq2e t493;
        gm_f_format_15kpcel((void *)&t493, (void *)NULL, cx, NULL, t492);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t493))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t493) + 8, gm_src2j6ljeja7ilp, 626, 33);
        gm_b16_8 t494 = GM_LD(gm_b16_8, (((uint8_t *)&t493) + 8));
        gm_b16_8 t495 = GM_LD(gm_b16_8, ((uint8_t *)&t494));
        gm_t08olq97rscn3 t496;
        gm_f_unsupported_531q51r((void *)&t496, (void *)NULL, cx, NULL, t486, t495);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t496);
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
__attribute__((aligned(4))) void gm_f_from_tag_2m7nfln(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_from_tag_2m7nfln F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* at */
    gm_b16_8 gm_l3; /* v0 */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* u */
    gm_b16_8 gm_l6; /* variants */
    gm_b16_8 gm_l7; /* res0 */
    gm_b16_8 gm_l8; /* res */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    gm_b16_8 gm_l13; /* lit */
    gm_b16_8 gm_l14; /* x */
    gm_b16_8 gm_l15; /* s */
    GM_STACK_CHECK(&F, 996u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        void * t7 = (void *)(((uint8_t *)t6) + 8);
        gm_t88rcucg1b7uk t8 = gm_l4;
        gm_t53at80pfdus3 t9;
        gm_f_kind_ghvav22((void *)&t9, t7, GM_CONV(gm_csguankko4lmhd, t8));
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t10 != ((uint8_t)13ULL)) goto ie4;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t9) + 8));
        gm_l5 = t11;
        {
            void * t14 = gm_l0;
            void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
            void * t16 = (void *)(((uint8_t *)t15) + 8);
            gm_t88rcucg1b7uk t17 = gm_l5;
            void * t18 = gm_f_adt_of_gej99vn(t16, GM_CONV(gm_csguankko4lmhd, t17));
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t18) + 24) + 0));
            if (t19 != ((uint8_t)1ULL)) goto ie13;
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((((uint8_t *)t18) + 24) + 8) + 8));
            gm_l6 = t20;
            {
                void * t22 = gm_l0;
                gm_t88rcucg1b7uk t23 = gm_l4;
                gm_b16_8 t24 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t22, t23);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t24;
            }
            {
                void * t25 = gm_l0;
                gm_t88rcucg1b7uk t26 = gm_l4;
                gm_b16_8 t27 = gm_l7;
                gm_b16_8 t28 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t25, t26, t27);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t28;
            }
            {
                {
                    gm_l10 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_l9 = t30;
                }
                {
                    for (;;) {
                        uint64_t t32 = gm_l10;
                        uint64_t t33 = gm_l9;
                        uint8_t t34 = (t32 < t33);
                        if (!t34) goto lb31;
                        {
                            uint64_t t37 = gm_l10;
                            uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&t37));
                            gm_l11 = t40;
                            {
                                uint64_t t42 = gm_l11;
                                uint64_t t43 = (uintptr_t)t42;
                                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_src2j6ljeja7ilp, 284, 15);
                                gm_tckdobunaulee t45 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t43 * 72));
                                F.l12 = t45;
                            }
                            {
                                uint8_t t47;
                                if ((((((uint8_t *)&F.l12) + 16))[4] != 0)) goto mn49;
                                t47 = ((uint8_t)0);
                                goto me48;
mn49:;
                                t47 = ((uint8_t)1);
                                goto me48;
mn50:;
                                gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 285, 12);
me48:;
                                if (!t47) goto ie46;
                                {
                                    goto lc31;
                                }
                                goto ix46;
ie46:;
ix46:;
                            }
                            {
                                void * t52 = gm_l0;
                                gm_t88rcucg1b7uk t53 = gm_l2;
                                gm_b16_8 t54 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t52, t53, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l12) + 24)));
                                if (gm_unw.kind) {
                                    goto gm_ll35;
                                }
                                gm_l13 = t54;
                            }
                            {
                                void * t55 = gm_l0;
                                gm_t6j6smj7qoctu t56;
                                gm_b16_8 t57 = gm_l3;
                                GM_ST(gm_b16_8, (uint8_t *)&t56 + 0, t57);
                                gm_b16_8 t58 = gm_l13;
                                GM_ST(gm_b16_8, (uint8_t *)&t56 + 16, t58);
                                gm_b0_1 t59 = gm_f_linef_156hint((void *)NULL, cx, NULL, t55, &t56);
                                if (gm_unw.kind) {
                                    goto gm_ll35;
                                }
                            }
                            {
                                void * t60 = gm_l0;
                                gm_t88rcucg1b7uk t61 = gm_l5;
                                uint64_t t62 = gm_l11;
                                gm_th2lqbo77pvsc t63;
                                memset(&t63, 0, sizeof t63);
                                gm_b16_8 t64 = gm_f_make_variant_srnk8ha((void *)NULL, cx, NULL, t60, t61, t62, &t63);
                                if (gm_unw.kind) {
                                    goto gm_ll35;
                                }
                                gm_l14 = t64;
                            }
                            {
                                void * t65 = gm_l0;
                                gm_t88rcucg1b7uk t66 = gm_l4;
                                gm_b16_8 t67 = gm_l14;
                                gm_b16_8 t68 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t65, t66, t67);
                                if (gm_unw.kind) {
                                    goto gm_ll35;
                                }
                                gm_l15 = t68;
                            }
                            {
                                void * t69 = gm_l0;
                                gm_t6j6smj7qoctu t70;
                                gm_b16_8 t71 = gm_l8;
                                GM_ST(gm_b16_8, (uint8_t *)&t70 + 0, t71);
                                gm_b16_8 t72 = gm_l15;
                                GM_ST(gm_b16_8, (uint8_t *)&t70 + 16, t72);
                                gm_b0_1 t73 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t69, &t70);
                                if (gm_unw.kind) {
                                    goto gm_ll35;
                                }
                            }
                            {
                                void * t74 = gm_l0;
                                gm_b16_8 t75;
                                GM_ST(void *, (uint8_t *)&t75, (void *)(gm_s2cuflhqkkuaq));
                                GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(1));
                                gm_b0_1 t76 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t74, t75);
                                if (gm_unw.kind) {
                                    goto gm_ll35;
                                }
                            }
                            goto me38;
mn39:;
                            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 283, 5);
me38:;
                        }
lc31:;
                        {
                            uint64_t t78 = gm_l10;
                            uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                            uint64_t t81 = r79;
                            gm_l10 = t81;
                        }
                    }
gm_ll35:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb31; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc31; }
                    }
                    goto gm_lad;
lb31:;
                }
            }
            {
                gm_b16_8 t82 = gm_l8;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t82);
                goto gm_fdone;
            }
            goto ix13;
ie13:;
            {
                void * t84 = gm_l1;
                gm_tl874t2atnlh3 t85 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t84) + 72));
                gm_b16_8 t86;
                GM_ST(void *, (uint8_t *)&t86, (void *)(gm_s66fk044n1n0k));
                GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(8));
                gm_t08olq97rscn3 t87;
                gm_f_unsupported_531q51r((void *)&t87, (void *)NULL, cx, NULL, t85, t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t87);
                goto gm_fdone;
            }
ix13:;
        }
        goto ix4;
ie4:;
        {
            void * t89 = gm_l1;
            gm_tl874t2atnlh3 t90 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t89) + 72));
            gm_b16_8 t91;
            GM_ST(void *, (uint8_t *)&t91, (void *)(gm_s66fk044n1n0k));
            GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(8));
            gm_t08olq97rscn3 t92;
            gm_f_unsupported_531q51r((void *)&t92, (void *)NULL, cx, NULL, t90, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t92);
            goto gm_fdone;
        }
ix4:;
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
__attribute__((aligned(4))) void gm_f_pow_s0lbtt4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_pow_s0lbtt4 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_t9qkh0rsooc8d gm_l2; /* fl */
    gm_t88rcucg1b7uk gm_l3; /* at */
    gm_b16_8 gm_l4; /* vals */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* v0 */
    gm_b16_8 gm_l7; /* loc */
    gm_tuglhjjmj2g58 gm_l8; /* ii */
    gm_b16_8 gm_l9; /* c */
    gm_b16_8 gm_l10; /* r */
    gm_b16_8 gm_l11; /* o */
    gm_b16_8 gm_l12; /* f */
    gm_b16_8 gm_l13; /* min */
    gm_b16_8 gm_l14; /* max */
    gm_b16_8 gm_l15; /* b */
    gm_b16_8 gm_l16; /* sat */
    GM_STACK_CHECK(&F, 1988u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l5 = t3;
    }
    {
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_src2j6ljeja7ilp, 342, 11);
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t4 * 16));
        gm_l6 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = gm_l1;
        gm_b16_8 t9 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t9;
    }
    {
        void * t10 = gm_l0;
        gm_t88rcucg1b7uk t11 = gm_l3;
        gm_tqcc6dmlne11b t12;
        gm_f_int_info_2tcdf1n((void *)&t12, (void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tuglhjjmj2g58 t13;
        if (((((uint8_t *)&t12))[16] != 0)) {
            gm_tuglhjjmj2g58 t14 = GM_LD(gm_tuglhjjmj2g58, (((uint8_t *)&t12) + 0));
            t13 = t14;
        } else {
            void * t15 = gm_l1;
            gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t15) + 72));
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s9gk1b0ve64mt));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(9));
            gm_t08olq97rscn3 t18;
            gm_f_unsupported_531q51r((void *)&t18, (void *)NULL, cx, NULL, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t18);
            goto gm_fdone;
        }
        gm_l8 = t13;
    }
    {
        gm_tuglhjjmj2g58 t19 = gm_l8;
        gm_b16_8 t20 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t19));
        gm_l9 = t20;
    }
    {
        void * t21 = gm_l0;
        uint32_t t22 = gm_f_fresh_5kokjmg(t21);
        gm_t88rcucg1b7uk t23;
        GM_ST(uint32_t, ((uint8_t *)&t23 + 0), t22);
        gm_tabb710d9mq2e t24;
        gm_f_format_jagvq3q((void *)&t24, (void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_src2j6ljeja7ilp, 346, 10);
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
        gm_l10 = t25;
    }
    {
        void * t26 = gm_l0;
        uint32_t t27 = gm_f_fresh_5kokjmg(t26);
        gm_t88rcucg1b7uk t28;
        GM_ST(uint32_t, ((uint8_t *)&t28 + 0), t27);
        gm_tabb710d9mq2e t29;
        gm_f_format_v2sfrt0((void *)&t29, (void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_src2j6ljeja7ilp, 347, 10);
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
        gm_l11 = t30;
    }
    {
        gm_t3877pk7lv748 t31;
        gm_b16_8 t32;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (!t34) goto ie33;
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sitlbs33jqqal));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(1));
        gm_b16_8 t37 = t36;
        t32 = t37;
        goto ix33;
ie33:;
        gm_b16_8 t39;
        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s0m9d22aen6f8));
        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(1));
        gm_b16_8 t40 = t39;
        t32 = t40;
ix33:;
        GM_ST(gm_b16_8, (uint8_t *)&t31 + 0, t32);
        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
        uint64_t r42; bool o43 = __builtin_mul_overflow(t41, ((uint64_t)8ULL), &r42);
        if (o43) gm_trap("integer overflow in `*`", gm_src2j6ljeja7ilp, 348, 71);
        uint64_t t44 = r42;
        GM_ST(uint64_t, (uint8_t *)&t31 + 16, t44);
        gm_tabb710d9mq2e t45;
        gm_f_format_gf4f6rt((void *)&t45, (void *)NULL, cx, NULL, &t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_src2j6ljeja7ilp, 348, 10);
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
        gm_l12 = t46;
    }
    {
        void * t47 = gm_l0;
        gm_th2d9c7aju8hp t48;
        gm_b16_8 t49 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t48 + 0, t49);
        gm_b16_8 t50 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t48 + 16, t50);
        gm_b16_8 t51 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t48 + 32, t51);
        gm_b16_8 t52 = gm_l12;
        GM_ST(gm_b16_8, (uint8_t *)&t48 + 48, t52);
        gm_b16_8 t53 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t48 + 64, t53);
        uint64_t t54 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t54 >= (uintptr_t)(t55)) gm_trap_index(t54, t55, gm_src2j6ljeja7ilp, 349, 85);
        gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t54 * 16));
        GM_ST(gm_b16_8, (uint8_t *)&t48 + 80, t56);
        gm_b16_8 t57 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t48 + 96, t57);
        gm_b0_1 t58 = gm_f_linef_ahjvru7((void *)NULL, cx, NULL, t47, &t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tuglhjjmj2g58 t60 = gm_l8;
        uint8_t t61 = gm_f_ii_exact_g6a19dp(GM_CONV(gm_cs2lqoac0a5ng4, t60));
        uint8_t t62 = (uint8_t)!t61;
        if (!t62) goto ie59;
        {
            void * t64 = gm_l0;
            gm_t88rcucg1b7uk t65 = gm_l3;
            gm_tuglhjjmj2g58 t66 = gm_l8;
            gm_tbci5ospk5nnn t67;
            gm_f_ii_min_lku7pdi((void *)&t67, (void *)NULL, cx, NULL, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t68 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t64, t65, &t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t68;
        }
        {
            void * t69 = gm_l0;
            gm_t88rcucg1b7uk t70 = gm_l3;
            gm_tuglhjjmj2g58 t71 = gm_l8;
            gm_tbci5ospk5nnn t72;
            gm_f_ii_max_gtp5h90((void *)&t72, (void *)NULL, cx, NULL, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t73 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t69, t70, &t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t73;
        }
        {
            void * t74 = gm_l0;
            gm_tif3elqk01bdl t75;
            gm_b16_8 t76 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t75 + 0, t76);
            gm_b16_8 t77 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t75 + 16, t77);
            gm_b16_8 t78 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t75 + 32, t78);
            gm_b16_8 t79 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t75 + 48, t79);
            gm_b16_8 t80 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t75 + 64, t80);
            gm_b16_8 t81 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t75 + 80, t81);
            gm_b0_1 t82 = gm_f_linef_calnga5((void *)NULL, cx, NULL, t74, &t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        uint8_t t84 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        if (!t84) goto ie83;
        void * t85 = gm_l0;
        void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
        uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)t86) + 1136) + 0));
        if (!t87) goto ie83;
        {
            void * t89 = gm_l0;
            gm_t88rcucg1b7uk t90 = gm_l3;
            gm_b16_8 t91 = gm_l6;
            gm_b16_8 t92 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t89, t90, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t92;
        }
        {
            void * t93 = gm_l0;
            gm_t8fvq98mjik04 t94;
            memset((uint8_t *)&t94 + 37, 0, 3);
            gm_b16_8 t95 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 0, t95);
            gm_b16_8 t96 = gm_l15;
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 16, t96);
            uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 4));
            GM_ST(uint32_t, (uint8_t *)&t94 + 32, t97);
            uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            GM_ST(uint8_t, (uint8_t *)&t94 + 36, t98);
            uint64_t t99 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t99 >= (uintptr_t)(t100)) gm_trap_index(t99, t100, gm_src2j6ljeja7ilp, 358, 116);
            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t99 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 40, t101);
            gm_b16_8 t102 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 56, t102);
            gm_b0_1 t103 = gm_f_linef_3utfuce((void *)NULL, cx, NULL, t93, &t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix83;
ie83:;
ix83:;
    }
    {
        gm_t6j6smj7qoctu t104;
        gm_b16_8 t105 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 0, t105);
        uint64_t t106 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t106 >= (uintptr_t)(t107)) gm_trap_index(t106, t107, gm_src2j6ljeja7ilp, 360, 56);
        gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t106 * 16));
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 16, t108);
        gm_tabb710d9mq2e t109;
        gm_f_format_qofaffd((void *)&t109, (void *)NULL, cx, NULL, &t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 8, gm_src2j6ljeja7ilp, 360, 12);
        gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
        gm_l16 = t110;
    }
    {
        void * t111 = gm_l0;
        gm_t9qkh0rsooc8d t112 = gm_l2;
        gm_tuglhjjmj2g58 t113 = gm_l8;
        gm_t88rcucg1b7uk t114 = gm_l3;
        gm_t88rcucg1b7uk t115 = gm_l5;
        gm_b16_8 t116 = gm_l10;
        gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
        gm_b16_8 t118 = gm_l11;
        gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
        gm_b16_8 t120 = gm_l16;
        gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
        gm_b16_8 t122 = gm_l7;
        gm_b16_8 t123;
        GM_ST(void *, (uint8_t *)&t123, (void *)(gm_svpofgjtij7vj));
        GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(3));
        gm_t5o0tpv5jka2l t124;
        gm_f_finish_int_cr0p55i((void *)&t124, (void *)NULL, cx, NULL, t111, t112, t113, t114, t115, t117, t119, t121, t122, t123);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t124))[0] != 0)) {
            gm_t5o0tpv5jka2l t125;
            GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
            ((uint8_t *)&t125)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t125 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t124) + 8)));
            F.ret = t125;
            goto gm_fret;
        }
        gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t126);
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
inline __attribute__((aligned(4))) void gm_f_rotate_tpbpt1c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_rotate_tpbpt1c F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* at */
    gm_b16_8 gm_l4; /* vals */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* v0 */
    gm_tuglhjjmj2g58 gm_l7; /* ii */
    gm_b16_8 gm_l8; /* x */
    gm_b16_8 gm_l9; /* k0 */
    gm_b16_8 gm_l10; /* k */
    gm_b16_8 gm_l11; /* c */
    gm_b16_8 gm_l12; /* r */
    gm_b16_8 gm_l13; /* n */
    GM_STACK_CHECK(&F, 1005u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l5 = t3;
    }
    {
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_src2j6ljeja7ilp, 299, 11);
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t4 * 16));
        gm_l6 = t6;
    }
    {
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l3;
        gm_tqcc6dmlne11b t9;
        gm_f_int_info_2tcdf1n((void *)&t9, (void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t9))[16] != 0)) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 300, 11);
        gm_tuglhjjmj2g58 t10 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t9));
        gm_l7 = t10;
    }
    {
        gm_t6j6smj7qoctu t11;
        gm_tuglhjjmj2g58 t12 = gm_l7;
        gm_b16_8 t13 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t12));
        GM_ST(gm_b16_8, (uint8_t *)&t11 + 0, t13);
        gm_b16_8 t14 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t11 + 16, t14);
        gm_tabb710d9mq2e t15;
        gm_f_format_21vnq4a((void *)&t15, (void *)NULL, cx, NULL, &t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_src2j6ljeja7ilp, 301, 10);
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
        gm_l8 = t16;
    }
    {
        uint64_t t17 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_src2j6ljeja7ilp, 302, 38);
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t17 * 16));
        gm_tnpifv20irnph t20;
        GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t19);
        gm_tabb710d9mq2e t21;
        gm_f_format_h95c7u1((void *)&t21, (void *)NULL, cx, NULL, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_src2j6ljeja7ilp, 302, 11);
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
        gm_l9 = t22;
    }
    {
        gm_b16_8 t23;
        uint8_t t25 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)32ULL)));
        if (!t25) goto ie24;
        gm_b16_8 t27 = gm_l9;
        gm_b16_8 t28 = t27;
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        t23 = t29;
        goto ix24;
ie24:;
        gm_t5g5vgung6h6s t31;
        memset((uint8_t *)&t31 + 4, 0, 4);
        memset((uint8_t *)&t31 + 28, 0, 4);
        uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
        GM_ST(uint32_t, (uint8_t *)&t31 + 0, t32);
        gm_b16_8 t33 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t31 + 8, t33);
        uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
        GM_ST(uint32_t, (uint8_t *)&t31 + 24, t34);
        gm_tabb710d9mq2e t35;
        gm_f_format_aghnr00((void *)&t35, (void *)NULL, cx, NULL, &t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_src2j6ljeja7ilp, 304, 46);
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
        gm_b16_8 t37 = t36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        t23 = t38;
ix24:;
        gm_l10 = t23;
    }
    {
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
        void * t41 = (void *)(((uint8_t *)t40) + 8);
        gm_t88rcucg1b7uk t42 = gm_l5;
        gm_b16_8 t43 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t41, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t43;
    }
    {
        void * t44 = gm_l0;
        gm_t88rcucg1b7uk t45 = gm_l5;
        gm_tj4tbafenpsjm t46;
        memset((uint8_t *)&t46 + 52, 0, 4);
        gm_b16_8 t47 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 0, t47);
        gm_b16_8 t48 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 16, t48);
        gm_b16_8 t49 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 32, t49);
        uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
        GM_ST(uint32_t, (uint8_t *)&t46 + 48, t50);
        gm_tabb710d9mq2e t51;
        gm_f_format_898275p((void *)&t51, (void *)NULL, cx, NULL, &t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_src2j6ljeja7ilp, 306, 20);
        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
        gm_b16_8 t54 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t44, t45, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t54;
    }
    {
        gm_tuglhjjmj2g58 t55 = gm_l7;
        gm_b16_8 t56 = gm_l12;
        gm_b16_8 t57 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t57;
    }
    {
        void * t58 = gm_l0;
        gm_t88rcucg1b7uk t59 = gm_l5;
        gm_b16_8 t60 = gm_l13;
        gm_b16_8 t61 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t58, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t61);
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
__attribute__((aligned(4))) void gm_f_sys_builtin_svdoho3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3) {
    struct gm_fr_sys_builtin_svdoho3 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_t9qkh0rsooc8d gm_l2; /* sop */
    gm_b16_8 gm_l3; /* args */
    gm_t88rcucg1b7uk gm_l4; /* t */
    uint32_t gm_l5; /* k */
    uint32_t gm_l6; /* k */
    gm_b16_8 gm_l7; /* v */
    gm_b16_8 gm_l8; /* addr */
    gm_b16_8 gm_l9; /* s */
    gm_b16_8 gm_l10; /* some */
    gm_b16_8 gm_l11; /* none */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    void * gm_l16; /* a */
    gm_b16_8 gm_l17; /* v */
    gm_b16_8 gm_l18; /* addr */
    gm_b16_8 gm_l19; /* call */
    GM_STACK_CHECK(&F, 2068u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)44ULL)));
        if (!t5) goto ie4;
        {
            void * t7 = gm_l0;
            uint32_t t8 = gm_f_fresh_5kokjmg(t7);
            gm_l5 = t8;
        }
        {
            void * t9 = gm_l0;
            uint32_t t10 = gm_l5;
            uint32_t t11 = gm_l5;
            uint32_t t12 = gm_l5;
            gm_tl874t2atnlh3 t13;
            GM_ST(uint32_t, ((uint8_t *)&t13 + 0), t10);
            GM_ST(uint32_t, ((uint8_t *)&t13 + 4), t11);
            GM_ST(uint32_t, ((uint8_t *)&t13 + 8), t12);
            gm_b0_1 t14 = gm_f_linef_5q2h82g((void *)NULL, cx, NULL, t9, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t15 = gm_l0;
            gm_t88rcucg1b7uk t16 = gm_l4;
            uint32_t t17 = gm_l5;
            gm_t88rcucg1b7uk t18;
            GM_ST(uint32_t, ((uint8_t *)&t18 + 0), t17);
            gm_tabb710d9mq2e t19;
            gm_f_format_ucng4n7((void *)&t19, (void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_src2j6ljeja7ilp, 223, 33);
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            uint32_t t22 = gm_l5;
            gm_t88rcucg1b7uk t23;
            GM_ST(uint32_t, ((uint8_t *)&t23 + 0), t22);
            gm_tabb710d9mq2e t24;
            gm_f_format_3itad0c((void *)&t24, (void *)NULL, cx, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_src2j6ljeja7ilp, 223, 59);
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            gm_b16_8 t27 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t15, t16, t21, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t27);
            goto gm_fdone;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint8_t t29 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)45ULL)));
        if (!t29) goto ie28;
        {
            void * t31 = gm_l0;
            uint32_t t32 = gm_f_fresh_5kokjmg(t31);
            gm_l6 = t32;
        }
        {
            void * t33 = gm_l0;
            uint64_t t34 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_src2j6ljeja7ilp, 227, 27);
            void * t36 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t34 * 88);
            gm_t5o0tpv5jka2l t37;
            gm_f_expr_porbc9t((void *)&t37, (void *)NULL, cx, NULL, t33, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t37))[0] != 0)) {
                gm_t5o0tpv5jka2l t38;
                GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                ((uint8_t *)&t38)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t38 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t37) + 8)));
                F.ret = t38;
                goto gm_fret;
            }
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
            gm_l7 = t39;
        }
        {
            void * t40 = gm_l0;
            uint64_t t41 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_src2j6ljeja7ilp, 228, 28);
            gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t41 * 88) + 0));
            gm_b16_8 t44 = gm_l7;
            gm_b16_8 t45 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t40, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t45;
        }
        {
            void * t46 = gm_l0;
            uint32_t t47 = gm_l6;
            uint32_t t48 = gm_l6;
            gm_tcc1rq4n1avb2 t49;
            GM_ST(uint32_t, ((uint8_t *)&t49 + 0), t47);
            GM_ST(uint32_t, ((uint8_t *)&t49 + 4), t48);
            gm_b0_1 t50 = gm_f_linef_vgjqckp((void *)NULL, cx, NULL, t46, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t51 = gm_l0;
            gm_t133qiba7tvhc t52;
            memset((uint8_t *)&t52 + 4, 0, 4);
            uint32_t t53 = gm_l6;
            GM_ST(uint32_t, (uint8_t *)&t52 + 0, t53);
            gm_b16_8 t54 = gm_l8;
            gm_b16_8 t55 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t52 + 8, t55);
            void * t56 = gm_l0;
            gm_b16_8 t57 = gm_l8;
            gm_b16_8 t58 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t52 + 24, t58);
            uint32_t t59 = gm_l6;
            GM_ST(uint32_t, (uint8_t *)&t52 + 40, t59);
            uint32_t t60 = gm_l6;
            GM_ST(uint32_t, (uint8_t *)&t52 + 44, t60);
            gm_b0_1 t61 = gm_f_linef_7pb5lg6((void *)NULL, cx, NULL, t51, &t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t62 = gm_l0;
            void * t63 = gm_l0;
            void * t64 = GM_LD(void *, (((uint8_t *)t63) + 0));
            void * t65 = (void *)(((uint8_t *)t64) + 8);
            gm_t88rcucg1b7uk t66 = gm_f_string_ty_kk94re3((void *)NULL, cx, NULL, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t67 = gm_l6;
            gm_t88rcucg1b7uk t68;
            GM_ST(uint32_t, ((uint8_t *)&t68 + 0), t67);
            gm_tabb710d9mq2e t69;
            gm_f_format_tvr4di5((void *)&t69, (void *)NULL, cx, NULL, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_src2j6ljeja7ilp, 231, 49);
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
            uint32_t t72 = gm_l6;
            gm_t88rcucg1b7uk t73;
            GM_ST(uint32_t, ((uint8_t *)&t73 + 0), t72);
            gm_tabb710d9mq2e t74;
            gm_f_format_he769rg((void *)&t74, (void *)NULL, cx, NULL, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 8, gm_src2j6ljeja7ilp, 231, 73);
            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 8));
            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
            gm_b16_8 t77 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t62, t66, t71, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t77;
        }
        {
            void * t78 = gm_l0;
            gm_t88rcucg1b7uk t79 = gm_l4;
            gm_b16_8 t80 = gm_l9;
            gm_b16_8 t81 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t78, t79, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t81;
        }
        {
            void * t82 = gm_l0;
            gm_t88rcucg1b7uk t83 = gm_l4;
            gm_b16_8 t84 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t82, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t84;
        }
        {
            void * t85 = gm_l0;
            gm_t88rcucg1b7uk t86 = gm_l4;
            gm_thognv9odov03 t87;
            memset((uint8_t *)&t87 + 4, 0, 4);
            uint32_t t88 = gm_l6;
            GM_ST(uint32_t, (uint8_t *)&t87 + 0, t88);
            gm_b16_8 t89 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t87 + 8, t89);
            gm_b16_8 t90 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t87 + 24, t90);
            gm_tabb710d9mq2e t91;
            gm_f_format_cjdn0sp((void *)&t91, (void *)NULL, cx, NULL, &t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 8, gm_src2j6ljeja7ilp, 234, 26);
            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
            gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
            gm_b16_8 t94 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t85, t86, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t94);
            goto gm_fdone;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        gm_f_list_epvducg((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            gm_l13 = t96;
        }
        {
            for (;;) {
                uint64_t t98 = gm_l14;
                uint64_t t99 = gm_l13;
                uint8_t t100 = (t98 < t99);
                if (!t100) goto lb97;
                {
                    uint64_t t103 = gm_l14;
                    uint64_t t106 = GM_LD(uint64_t, ((uint8_t *)&t103));
                    gm_l15 = t106;
                    {
                        uint64_t t108 = gm_l15;
                        uint64_t t109 = (uintptr_t)t108;
                        uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t109 >= (uintptr_t)(t110)) gm_trap_index(t109, t110, gm_src2j6ljeja7ilp, 238, 15);
                        void * t111 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t109 * 88);
                        gm_l16 = t111;
                    }
                    {
                        void * t112 = gm_l0;
                        void * t113 = gm_l16;
                        gm_t5o0tpv5jka2l t114;
                        gm_f_expr_porbc9t((void *)&t114, (void *)NULL, cx, NULL, t112, t113);
                        if (gm_unw.kind) {
                            goto gm_ll101;
                        }
                        if (((((uint8_t *)&t114))[0] != 0)) {
                            gm_t5o0tpv5jka2l t115;
                            GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                            ((uint8_t *)&t115)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t115 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t114) + 8)));
                            F.ret = t115;
                            goto gm_fret;
                        }
                        gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
                        gm_l17 = t116;
                    }
                    {
                        void * t118 = gm_l0;
                        void * t119 = GM_LD(void *, (((uint8_t *)t118) + 0));
                        void * t120 = (void *)(((uint8_t *)t119) + 8);
                        void * t121 = gm_l16;
                        gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t121) + 0));
                        gm_t53at80pfdus3 t123;
                        gm_f_kind_ghvav22((void *)&t123, t120, GM_CONV(gm_csguankko4lmhd, t122));
                        uint8_t t124;
                        uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&t123) + 0));
                        if (t127 != ((uint8_t)10ULL)) goto mn126;
                        t124 = ((uint8_t)1);
                        goto me125;
mn126:;
                        t124 = ((uint8_t)0);
                        goto me125;
mn128:;
                        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 240, 12);
me125:;
                        if (!t124) goto ie117;
                        {
                            void * t130 = gm_l0;
                            void * t131 = gm_l16;
                            gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t131) + 0));
                            gm_b16_8 t133 = gm_l17;
                            gm_b16_8 t134 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t130, t132, t133);
                            if (gm_unw.kind) {
                                goto gm_ll101;
                            }
                            gm_l18 = t134;
                        }
                        {
                            void * t135 = (void *)((uint8_t *)&F.l12);
                            gm_t6j6smj7qoctu t136;
                            gm_b16_8 t137 = gm_l18;
                            gm_b16_8 t138 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t137);
                            if (gm_unw.kind) {
                                goto gm_ll101;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t136 + 0, t138);
                            void * t139 = gm_l0;
                            gm_b16_8 t140 = gm_l18;
                            gm_b16_8 t141 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t139, t140);
                            if (gm_unw.kind) {
                                goto gm_ll101;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t136 + 16, t141);
                            gm_tabb710d9mq2e t142;
                            gm_f_format_00b54tv((void *)&t142, (void *)NULL, cx, NULL, &t136);
                            if (gm_unw.kind) {
                                goto gm_ll101;
                            }
                            if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 8, gm_src2j6ljeja7ilp, 242, 24);
                            gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&t142) + 8));
                            gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                            gm_tftb1odc739t5 t145 = gm_f_push_3ef746b(t135, t144);
                            if (((((uint8_t *)&t145))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t145) + 1, gm_src2j6ljeja7ilp, 242, 13);
                            gm_b0_1 t146 = GM_LD(gm_b0_1, (((uint8_t *)&t145) + 1));
                        }
                        goto ix117;
ie117:;
                        {
                            void * t148 = (void *)((uint8_t *)&F.l12);
                            gm_b16_8 t149 = gm_l17;
                            gm_tftb1odc739t5 t150 = gm_f_push_3ef746b(t148, t149);
                            if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 1, gm_src2j6ljeja7ilp, 244, 13);
                            gm_b0_1 t151 = GM_LD(gm_b0_1, (((uint8_t *)&t150) + 1));
                        }
ix117:;
                    }
                    goto me104;
mn105:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 237, 5);
me104:;
                }
lc97:;
                {
                    uint64_t t153 = gm_l14;
                    uint64_t r154; bool o155 = __builtin_add_overflow(t153, ((uint64_t)1ULL), &r154);
                    uint64_t t156 = r154;
                    gm_l14 = t156;
                }
            }
gm_ll101:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb97; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc97; }
            }
            goto gm_lad;
lb97:;
        }
    }
    {
        gm_t6j6smj7qoctu t157;
        gm_t9qkh0rsooc8d t158 = gm_l2;
        gm_b16_8 t159 = gm_f_sys_name_pcdoujk(t158);
        GM_ST(gm_b16_8, (uint8_t *)&t157 + 0, t159);
        gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
        gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
        gm_b16_8 t162;
        GM_ST(void *, (uint8_t *)&t162, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t163;
        gm_f_join_sfknljb((void *)&t163, (void *)NULL, cx, NULL, t161, t162);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 8, gm_src2j6ljeja7ilp, 247, 56);
        gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&t163) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t157 + 16, t164);
        gm_tabb710d9mq2e t165;
        gm_f_format_m7gv7a8((void *)&t165, (void *)NULL, cx, NULL, &t157);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t165))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t165) + 8, gm_src2j6ljeja7ilp, 247, 13);
        gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&t165) + 8));
        gm_l19 = t166;
    }
    {
        void * t168 = gm_l0;
        void * t169 = GM_LD(void *, (((uint8_t *)t168) + 0));
        void * t170 = (void *)(((uint8_t *)t169) + 8);
        gm_t88rcucg1b7uk t171 = gm_l4;
        uint8_t t172 = gm_f_is_unit_17uennd(t170, GM_CONV(gm_csguankko4lmhd, t171));
        if (!t172) goto ie167;
        {
            void * t174 = gm_l0;
            gm_b16_8 t175 = gm_l19;
            gm_tnpifv20irnph t176;
            GM_ST(gm_b16_8, ((uint8_t *)&t176 + 0), t175);
            gm_b0_1 t177 = gm_f_linef_v355qja((void *)NULL, cx, NULL, t174, t176);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t178 = gm_l0;
            gm_b16_8 t179 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t178);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t179);
            goto gm_fdone;
        }
        goto ix167;
ie167:;
ix167:;
    }
    {
        void * t180 = gm_l0;
        gm_t88rcucg1b7uk t181 = gm_l4;
        gm_b16_8 t182 = gm_l19;
        gm_b16_8 t183 = GM_LD(gm_b16_8, ((uint8_t *)&t182));
        gm_b16_8 t184 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t180, t181, t183);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t184);
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
