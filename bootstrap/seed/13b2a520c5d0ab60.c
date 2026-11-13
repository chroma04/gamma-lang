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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct gm_cskjv6t053tkje { uint32_t f0; uint64_t f1; } gm_cskjv6t053tkje;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_cskjv6t053tkje) == 16 && _Alignof(gm_cskjv6t053tkje) == 8 && offsetof(gm_cskjv6t053tkje, f0) == 0 && offsetof(gm_cskjv6t053tkje, f1) == 8, "layout of `Char`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_tiilmfd16v3fc f0; gm_b16_8 f1; } gm_t1cs2fjnrqap1;
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; } gm_tb8ondtf8obk4;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbgd14esf0jvi v0; } f1; } gm_tgeh6n0c7c8kb;
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; } gm_tha5675rv512q;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_tiilmfd16v3fc f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; gm_b16_8 f3; } gm_tvc83pqnepc2e;
_Static_assert(sizeof(gm_t1cs2fjnrqap1) == 40 && _Alignof(gm_t1cs2fjnrqap1) == 8 && offsetof(gm_t1cs2fjnrqap1, f0) == 0 && offsetof(gm_t1cs2fjnrqap1, f1) == 24, "layout of `HandlerLit`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tb8ondtf8obk4) == 168 && _Alignof(gm_tb8ondtf8obk4) == 8 && offsetof(gm_tb8ondtf8obk4, f0) == 0 && offsetof(gm_tb8ondtf8obk4, f1) == 8 && offsetof(gm_tb8ondtf8obk4, f2) == 48 && offsetof(gm_tb8ondtf8obk4, f3) == 88 && offsetof(gm_tb8ondtf8obk4, f4) == 128, "layout of `StackAnalysis`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tgeh6n0c7c8kb) == 40 && _Alignof(gm_tgeh6n0c7c8kb) == 8 && offsetof(gm_tgeh6n0c7c8kb, f0) == 0 && offsetof(gm_tgeh6n0c7c8kb, f1) == 8, "layout of `StackFail`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
_Static_assert(sizeof(gm_tha5675rv512q) == 48 && _Alignof(gm_tha5675rv512q) == 8 && offsetof(gm_tha5675rv512q, f0) == 0 && offsetof(gm_tha5675rv512q, f1) == 8, "layout of `StackFrame`");
typedef struct { gm_tha5675rv512q f0; uint8_t f1; uint8_t pe[7]; } gm_tijr008e33o2m;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tgeh6n0c7c8kb v0; uint64_t v1; } f1; } gm_toabfckqu3onu;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tvc83pqnepc2e) == 64 && _Alignof(gm_tvc83pqnepc2e) == 8 && offsetof(gm_tvc83pqnepc2e, f0) == 0 && offsetof(gm_tvc83pqnepc2e, f1) == 24 && offsetof(gm_tvc83pqnepc2e, f2) == 32 && offsetof(gm_tvc83pqnepc2e, f3) == 48, "layout of `HandlerDeclInfo`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tijr008e33o2m) == 56 && _Alignof(gm_tijr008e33o2m) == 8 && offsetof(gm_tijr008e33o2m, f0) == 0 && offsetof(gm_tijr008e33o2m, f1) == 48, "layout of `?StackFrame`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
_Static_assert(sizeof(gm_toabfckqu3onu) == 48 && _Alignof(gm_toabfckqu3onu) == 8 && offsetof(gm_toabfckqu3onu, f0) == 0 && offsetof(gm_toabfckqu3onu, f1) == 8, "layout of `StackFail!u64`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
struct gm_fr_bind_fn_pefln9r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bound_of_sn02ojm {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_fn_t3eo0l3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_pop_d7svgs5 {
    gm_frame h;
    gm_tha5675rv512q l2; /* x */
    gm_tijr008e33o2m ret;
};
struct gm_fr_stack_call_poqmlno {
    gm_frame h;
    gm_tha5675rv512q l8; /* fr */
    gm_tdgfduqjheqdm l9; /* def */
    gm_trqfvmjtt4sij l11; /* bound */
    gm_trqfvmjtt4sij l21; /* saved */
    gm_toabfckqu3onu l22; /* r */
    gm_toabfckqu3onu ret;
};
struct gm_fr_stack_children_dedraea {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* kids */
    gm_b16_8 ret;
};
struct gm_fr_stack_expr_33aekgt {
    gm_frame h;
    gm_tsth9qu1dv0g3 l3; /* callee */
    gm_tntn2tf15puib l14; /* w */
    gm_t710lsrq1m8gk l23; /* w */
    gm_trqfvmjtt4sij l24; /* installed */
    gm_t1cs2fjnrqap1 l30; /* lit */
    gm_tvc83pqnepc2e l36; /* info */
    gm_t1cs2fjnrqap1 l40; /* h */
    gm_toabfckqu3onu l47; /* r */
    gm_tha5675rv512q l49; /* top */
    gm_tdgfduqjheqdm l50; /* def */
    gm_toabfckqu3onu ret;
};
struct gm_fr_stack_frame_68512ca {
    gm_frame h;
    gm_tdgfduqjheqdm l5; /* def */
    uint64_t ret;
};
struct gm_fr_stack_perform_soivemm {
    gm_frame h;
    gm_t1cs2fjnrqap1 l8; /* h */
    gm_toabfckqu3onu ret;
};
struct gm_fr_stack_push_expr_bdl56rb {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* bound */
    gm_toabfckqu3onu l4; /* r */
    gm_toabfckqu3onu ret;
};
struct gm_fr_stack_size_0gijrqq {
    gm_frame h;
    gm_tb8ondtf8obk4 l3; /* a */
    gm_toabfckqu3onu l4; /* r */
    gm_trqfvmjtt4sij l7; /* chain */
    gm_tha5675rv512q l10; /* fr */
    gm_t4gut2c9fj1kt l12; /* d */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_stack_target_7vr78v6 {
    gm_frame h;
    gm_tha5675rv512q l6; /* top */
    gm_tha5675rv512q l9; /* top */
    gm_t9805qtonbsag ret;
};
__attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_fn_pefln9r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_bound_of_sn02ojm(void * a0, uint32_t a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vndn3a4(void);
__attribute__((aligned(4))) uint8_t gm_f_ensure_body_tbtojbv(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_2kurgou(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_t3eo0l3(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_845n386(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hp1rthv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_koh61mc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n18n34e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t68frbc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_frame_cost_oei64tv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_msjtuv4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_o00nc4s(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_pe70gj7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_vk99g9b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_mentions_error_37knu7r(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_d7svgs5(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_12ij5jo(void * a0, gm_cs9ufftihtbpo2 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1mcf4ec(void * a0, gm_cskjv6t053tkje a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_9gqhmau(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_njfep1h(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_stack_call_poqmlno(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_stack_children_dedraea(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_stack_expr_33aekgt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint64_t gm_f_stack_frame_68512ca(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_stack_perform_soivemm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_stack_push_expr_bdl56rb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_stack_size_0gijrqq(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_stack_target_7vr78v6(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_truncate_31h1fac(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
extern uint8_t gm_cb7r52n9k7f[8] __attribute__((aligned(8)));
extern uint8_t gm_cdlh2b67kra[8] __attribute__((aligned(8)));
extern uint8_t gm_cilec57f4ei[8] __attribute__((aligned(8)));
extern const uint8_t gm_s2sodn6uclrlt[63];
extern const uint8_t gm_s9impgj2b5t1c[50];
extern const uint8_t gm_sdscrog2npict[23];
extern const uint8_t gm_sdsrtcf8ng48d[6];
extern const uint8_t gm_sg0997vvtiund[65];
extern const char gm_src4tam33qo20j7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_d7svgs5(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_d7svgs5 F;
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
            gm_tijr008e33o2m t8;
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
        gm_tha5675rv512q t17 = GM_LD(gm_tha5675rv512q, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 48));
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
        gm_tha5675rv512q t29 = F.l2;
        gm_tijr008e33o2m t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 52, 0);
        GM_ST(gm_tha5675rv512q, ((uint8_t *)&t30), t29);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_bind_fn_pefln9r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_bind_fn_pefln9r F;
    void * gm_l0; /* bound */
    uint32_t gm_l1; /* local */
    uint32_t gm_l2; /* f */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 219u);
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
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
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
                        uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)t20) + 0) + 8);
                        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_src4tam33qo20j7, 272, 12);
                        uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t20) + 0)) + t19 * 8) + 0));
                        uint32_t t23 = gm_l1;
                        uint8_t t24 = ((t22 == t23));
                        if (!t24) goto ie17;
                        {
                            uint64_t t26 = gm_l5;
                            uint64_t t27 = (uintptr_t)t26;
                            void * t28 = gm_l0;
                            uint64_t t29 = GM_LD(uintptr_t, (((uint8_t *)t28) + 0) + 8);
                            if (t27 >= (uintptr_t)(t29)) gm_trap_index(t27, t29, gm_src4tam33qo20j7, 273, 13);
                            uint32_t t30 = gm_l2;
                            GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t28) + 0)) + t27 * 8) + 4), t30);
                        }
                        {
                            gm_b0_1 t31;
                            uint8_t t32 = 0;
                            F.ret = t31;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 271, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t34 = gm_l4;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l4 = t37;
                }
            }
lb5:;
        }
    }
    {
        void * t38 = gm_l0;
        uint32_t t39 = gm_l1;
        uint32_t t40 = gm_l2;
        gm_tcc1rq4n1avb2 t41;
        GM_ST(uint32_t, ((uint8_t *)&t41 + 0), t39);
        GM_ST(uint32_t, ((uint8_t *)&t41 + 4), t40);
        gm_tftb1odc739t5 t42 = gm_f_push_12ij5jo(t38, GM_CONV(gm_cs9ufftihtbpo2, t41));
        gm_tftb1odc739t5 t43 = t42;
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_src4tam33qo20j7, 277, 5);
        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_bound_of_sn02ojm(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bound_of_sn02ojm F;
    void * gm_l0; /* bound */
    uint32_t gm_l1; /* local */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tcc1rq4n1avb2 gm_l4; /* b */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tcc1rq4n1avb2 t16 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 8));
                    gm_l4 = t16;
                    {
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
                        uint32_t t20 = gm_l1;
                        uint8_t t21 = ((t19 == t20));
                        if (!t21) goto ie18;
                        {
                            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
                            gm_t9805qtonbsag t24;
                            GM_ST(uint8_t, (uint8_t *)&t24 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t24 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t24), t23);
                            ((uint8_t *)&t24)[4] = 1;
                            uint8_t t25 = 0;
                            F.ret = t24;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 168, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t27 = gm_l3;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l3 = t30;
                }
            }
lb5:;
        }
    }
    {
        gm_t9805qtonbsag t31;
        memset(&t31, 0, sizeof t31);
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t3eo0l3(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_t3eo0l3 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = (void *)((uint8_t *)&((struct gm_fr_stack_children_dedraea *)((&F.h)->up))->l1);
        void * t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_push_pf02nv6(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_src4tam33qo20j7, 283, 42);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_stack_call_poqmlno(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_stack_call_poqmlno F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* g */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l3; /* env */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* name */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l10; /* body */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint32_t gm_l15; /* p */
    uint32_t gm_l16; /* t */
    uint8_t gm_l17; /* memoizable */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_tb31k5l794t7p gm_l20; /* m */
    uint64_t gm_l23; /* c */
    GM_STACK_CHECK(&F, 2455u);
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
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
        if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_src4tam33qo20j7, 86, 13);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t4 * 136) + 0));
        gm_l5 = t8;
    }
    {
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((((uint8_t *)t10) + 8) + 0));
            gm_l6 = t11;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l7;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l7;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_tha5675rv512q t23 = GM_LD(gm_tha5675rv512q, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t19 * 48));
                    F.l8 = t23;
                    {
                        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&F.l8) + 0));
                        uint32_t t27 = gm_l1;
                        uint8_t t28 = ((t26 == t27));
                        if (!t28) goto ie25;
                        {
                            gm_tbgd14esf0jvi t30;
                            memset((uint8_t *)&t30 + 12, 0, 4);
                            gm_tl874t2atnlh3 t31 = gm_l4;
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t30 + 0, t31);
                            gm_b16_8 t32 = gm_l5;
                            gm_tnpifv20irnph t33;
                            GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
                            gm_tabb710d9mq2e t34;
                            gm_f_format_n18n34e((void *)&t34, (void *)NULL, cx, NULL, t33);
                            if (gm_unw.kind) {
                                goto gm_ll16;
                            }
                            gm_tabb710d9mq2e t35 = t34;
                            if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_src4tam33qo20j7, 88, 51);
                            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
                            gm_b16_8 t37 = t36;
                            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                            GM_ST(gm_b16_8, (uint8_t *)&t30 + 16, t38);
                            gm_tgeh6n0c7c8kb t39;
                            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t39 + 8), t30);
                            gm_toabfckqu3onu t40;
                            GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                            ((uint8_t *)&t40)[0] = 1;
                            GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t40 + 8), t39);
                            gm_toabfckqu3onu t41 = t40;
                            uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
                            F.ret = t40;
                            goto gm_fret;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 87, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t44 = gm_l7;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l7 = t47;
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
        void * t49 = gm_l0;
        void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
        uint32_t t51 = gm_l1;
        gm_tl874t2atnlh3 t52 = gm_l4;
        uint8_t t53 = gm_f_ensure_body_tbtojbv((void *)NULL, cx, NULL, t50, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t54 = (uint8_t)!t53;
        if (!t54) goto ie48;
        {
            gm_tgeh6n0c7c8kb t56;
            memset(&t56, 0, sizeof t56);
            GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)0ULL));
            gm_toabfckqu3onu t57;
            GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
            ((uint8_t *)&t57)[0] = 1;
            GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t57 + 8), t56);
            gm_toabfckqu3onu t58 = t57;
            uint8_t t59 = ((((uint8_t *)&t58))[0] != 0);
            F.ret = t57;
            goto gm_fret;
        }
        goto ix48;
ie48:;
ix48:;
    }
    {
        void * t60 = gm_l0;
        void * t61 = GM_LD(void *, (((uint8_t *)t60) + 0));
        uint32_t t62 = gm_l1;
        gm_f_def_of_m512sbu((void *)&F.l9, t61, t62);
    }
    {
        void * t63 = GM_LD(void *, (((uint8_t *)&F.l9) + 40));
        void * t64 = t63;
        void * t65;
        if ((GM_LD(void *, ((uint8_t *)&t64)) != 0)) {
            void * t66 = GM_LD(void *, (((uint8_t *)&t64) + 0));
            t65 = t66;
        } else {
            {
                uint8_t t69 = ((GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0) + 0) == ((uint8_t)3ULL)));
                if (!t69) goto ie68;
                {
                    gm_tbgd14esf0jvi t71;
                    memset((uint8_t *)&t71 + 12, 0, 4);
                    gm_tl874t2atnlh3 t72 = gm_l4;
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t71 + 0, t72);
                    gm_b16_8 t73 = gm_l5;
                    gm_tnpifv20irnph t74;
                    GM_ST(gm_b16_8, ((uint8_t *)&t74 + 0), t73);
                    gm_tabb710d9mq2e t75;
                    gm_f_format_koh61mc((void *)&t75, (void *)NULL, cx, NULL, t74);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t76 = t75;
                    if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 8, gm_src4tam33qo20j7, 93, 62);
                    gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 8));
                    gm_b16_8 t78 = t77;
                    gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                    GM_ST(gm_b16_8, (uint8_t *)&t71 + 16, t79);
                    gm_tgeh6n0c7c8kb t80;
                    GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t80 + 8), t71);
                    gm_toabfckqu3onu t81;
                    GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
                    ((uint8_t *)&t81)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t81 + 8), t80);
                    gm_toabfckqu3onu t82 = t81;
                    uint8_t t83 = ((((uint8_t *)&t82))[0] != 0);
                    F.ret = t81;
                    goto gm_fret;
                }
                goto ix68;
ie68:;
ix68:;
            }
            {
                void * t84 = gm_l0;
                void * t85 = GM_LD(void *, (((uint8_t *)t84) + 0));
                gm_tl874t2atnlh3 t86 = gm_l4;
                gm_b16_8 t87;
                GM_ST(void *, (uint8_t *)&t87, (void *)(gm_s9impgj2b5t1c));
                GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(49));
                gm_b0_1 t88 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t85, t86, t87);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_tgeh6n0c7c8kb t89;
            memset(&t89, 0, sizeof t89);
            GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)0ULL));
            gm_toabfckqu3onu t90;
            GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
            ((uint8_t *)&t90)[0] = 1;
            GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t90 + 8), t89);
            gm_toabfckqu3onu t91 = t90;
            uint8_t t92 = ((((uint8_t *)&t91))[0] != 0);
            F.ret = t90;
            goto gm_fret;
        }
        gm_l10 = t65;
    }
    {
        gm_f_list_vk99g9b((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 8));
            gm_b16_8 t95 = t94;
            gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
            gm_l12 = t96;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t98 = gm_l13;
                uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t100 = (t98 < t99);
                if (!t100) goto lb97;
                {
                    uint64_t t103 = gm_l13;
                    uint64_t t104 = gm_l13;
                    uint64_t t105 = (uintptr_t)t104;
                    uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint32_t t107 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t105 * 4));
                    gm_tn68el8d14vku t108;
                    GM_ST(uint32_t, (uint8_t *)&t108 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t108 + 0), t103);
                    GM_ST(uint32_t, ((uint8_t *)&t108 + 8), t107);
                    gm_tn68el8d14vku t109 = t108;
                    uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&t109) + 0));
                    gm_l14 = t112;
                    uint32_t t113 = GM_LD(uint32_t, (((uint8_t *)&t109) + 8));
                    gm_l15 = t113;
                    {
                        uint64_t t116 = gm_l14;
                        uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        uint8_t t118 = (t116 < t117);
                        if (!t118) goto ie115;
                        {
                            void * t121 = gm_l0;
                            uint64_t t122 = gm_l14;
                            uint64_t t123 = (uintptr_t)t122;
                            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t123 >= (uintptr_t)(t124)) gm_trap_index(t123, t124, gm_src4tam33qo20j7, 100, 38);
                            void * t125 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t123 * 224);
                            gm_t9805qtonbsag t126 = gm_f_stack_target_7vr78v6(t121, t125);
                            gm_t9805qtonbsag t127 = t126;
                            if (!((((uint8_t *)&t127))[4] != 0)) goto ie120;
                            uint32_t t128 = GM_LD(uint32_t, ((uint8_t *)&t127));
                            gm_l16 = t128;
                            {
                                void * t130 = (void *)((uint8_t *)&F.l11);
                                uint32_t t131 = gm_l15;
                                uint32_t t132 = gm_l16;
                                gm_tcc1rq4n1avb2 t133;
                                GM_ST(uint32_t, ((uint8_t *)&t133 + 0), t131);
                                GM_ST(uint32_t, ((uint8_t *)&t133 + 4), t132);
                                gm_tftb1odc739t5 t134 = gm_f_push_12ij5jo(t130, GM_CONV(gm_cs9ufftihtbpo2, t133));
                                gm_tftb1odc739t5 t135 = t134;
                                if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_src4tam33qo20j7, 100, 49);
                                gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
                            }
                            goto ix120;
ie120:;
ix120:;
                        }
                        goto ix115;
ie115:;
ix115:;
                    }
                    goto me110;
mn111:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 98, 5);
me110:;
                }
lc97:;
                {
                    uint64_t t138 = gm_l13;
                    uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                    uint64_t t141 = r139;
                    gm_l13 = t141;
                }
            }
lb97:;
        }
    }
    {
        uint8_t t142 = 0;
        uint8_t t143 = 0;
        uint8_t t144;
        if ((((((uint8_t *)&F.l9) + 48))[4] != 0)) goto mn146;
        t144 = ((uint8_t)1);
        goto me145;
mn146:;
        t144 = ((uint8_t)0);
        goto me145;
mn147:;
        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 105, 19);
me145:;
        if (t144) {
            uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t149 = ((t148 == ((uint64_t)0ULL)));
            t143 = t149;
        } else t143 = 0;
        if (t143) {
            uint64_t t150 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
            uint8_t t151 = ((t150 == ((uint64_t)0ULL)));
            t142 = t151;
        } else t142 = 0;
        gm_l17 = t142;
    }
    {
        uint8_t t153 = gm_l17;
        if (!t153) goto ie152;
        {
            {
                void * t156 = gm_l0;
                gm_b16_8 t157 = GM_LD(gm_b16_8, ((((uint8_t *)t156) + 88) + 0));
                gm_l18 = t157;
            }
            {
                gm_l19 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t159 = gm_l19;
                    uint64_t t160 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    uint8_t t161 = (t159 < t160);
                    if (!t161) goto lb158;
                    {
                        uint64_t t164 = gm_l19;
                        uint64_t t165 = (uintptr_t)t164;
                        uint64_t t166 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                        gm_tb31k5l794t7p t169 = GM_LD(gm_tb31k5l794t7p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t165 * 16));
                        gm_l20 = t169;
                        {
                            uint32_t t172 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 0));
                            uint32_t t173 = gm_l1;
                            uint8_t t174 = ((t172 == t173));
                            if (!t174) goto ie171;
                            {
                                uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                gm_toabfckqu3onu t177;
                                memset(&t177, 0, sizeof t177);
                                ((uint8_t *)&t177)[0] = 0;
                                GM_ST(uint64_t, ((uint8_t *)&t177 + 8), t176);
                                gm_toabfckqu3onu t178 = t177;
                                uint8_t t179 = ((((uint8_t *)&t178))[0] != 0);
                                F.ret = t177;
                                goto gm_fret;
                            }
                            goto ix171;
ie171:;
ix171:;
                        }
                        goto me167;
mn168:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 107, 9);
me167:;
                    }
lc158:;
                    {
                        uint64_t t181 = gm_l19;
                        uint64_t r182; bool o183 = __builtin_add_overflow(t181, ((uint64_t)1ULL), &r182);
                        uint64_t t184 = r182;
                        gm_l19 = t184;
                    }
                }
lb158:;
            }
        }
        goto ix152;
ie152:;
ix152:;
    }
    {
        void * t185 = gm_l0;
        gm_trqfvmjtt4sij t186 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t185) + 48));
        F.l21 = t186;
    }
    {
        void * t187 = gm_l0;
        uint8_t *t188 = (((uint8_t *)t187) + 48);
        gm_f_list_o00nc4s((void *)&(*(gm_trqfvmjtt4sij *)t188), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t189 = gm_l0;
        void * t190 = (void *)(((uint8_t *)t189) + 48);
        gm_b16_8 t191 = gm_l3;
        gm_tftb1odc739t5 t192 = gm_f_extend_2kurgou(t190, t191);
        gm_tftb1odc739t5 t193 = t192;
        if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 1, gm_src4tam33qo20j7, 113, 5);
        gm_b0_1 t194 = GM_LD(gm_b0_1, (((uint8_t *)&t193) + 1));
    }
    {
        void * t195 = gm_l0;
        uint32_t t196 = gm_l1;
        void * t197 = gm_l10;
        gm_f_stack_push_expr_bdl56rb((void *)&F.l22, (void *)NULL, cx, NULL, t195, t196, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l11)), t197);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t198 = gm_l0;
        gm_trqfvmjtt4sij t199 = F.l21;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t198) + 48), t199);
    }
    {
        gm_toabfckqu3onu t200 = F.l22;
        gm_toabfckqu3onu t201 = t200;
        if (((((uint8_t *)&t201))[0] != 0)) {
            gm_tgeh6n0c7c8kb t202 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t201) + 8));
            gm_toabfckqu3onu t203;
            GM_ST(uint8_t, (uint8_t *)&t203 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t203 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t203 + 4, 0);
            ((uint8_t *)&t203)[0] = 1;
            GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t203 + 8), t202);
            F.ret = t203;
            goto gm_fret;
        }
        uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)&t201) + 8));
        void * t205 = gm_l0;
        uint32_t t206 = gm_l1;
        uint64_t t207 = gm_f_stack_frame_68512ca((void *)NULL, cx, NULL, t205, t206);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t r208; bool o209 = __builtin_add_overflow(t204, t207, &r208);
        uint64_t t210 = o209 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r208;
        gm_l23 = t210;
    }
    {
        uint8_t t212 = gm_l17;
        if (!t212) goto ie211;
        {
            void * t214 = gm_l0;
            void * t215 = (void *)(((uint8_t *)t214) + 88);
            uint32_t t216 = gm_l1;
            uint64_t t217 = gm_l23;
            gm_tb31k5l794t7p t218;
            GM_ST(uint32_t, (uint8_t *)&t218 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t218 + 0), t216);
            GM_ST(uint64_t, ((uint8_t *)&t218 + 8), t217);
            gm_tftb1odc739t5 t219 = gm_f_push_1mcf4ec(t215, GM_CONV(gm_cskjv6t053tkje, t218));
            gm_tftb1odc739t5 t220 = t219;
            if (((((uint8_t *)&t220))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t220) + 1, gm_src4tam33qo20j7, 117, 21);
            gm_b0_1 t221 = GM_LD(gm_b0_1, (((uint8_t *)&t220) + 1));
        }
        goto ix211;
ie211:;
ix211:;
    }
    {
        uint64_t t222 = gm_l23;
        gm_toabfckqu3onu t223;
        memset(&t223, 0, sizeof t223);
        ((uint8_t *)&t223)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t223 + 8), t222);
        gm_toabfckqu3onu t224 = t223;
        uint8_t t225 = ((((uint8_t *)&t224))[0] != 0);
        F.ret = t223;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_stack_children_dedraea(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_stack_children_dedraea F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_s1kpkpr((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        gm_closure t3 = ((gm_closure){(void *)gm_f_fn_t3eo0l3, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t5 = 0;
        F.ret = t4;
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
__attribute__((aligned(4))) void gm_f_stack_expr_33aekgt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stack_expr_33aekgt F;
    void * gm_l0; /* a */
    void * gm_l1; /* e */
    uint64_t gm_l2; /* m */
    gm_b16_8 gm_l4; /* args */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint64_t gm_l8; /* c */
    uint32_t gm_l9; /* g */
    void * gm_l10; /* f */
    uint32_t gm_l11; /* g */
    gm_tiilmfd16v3fc gm_l12; /* eref */
    uint32_t gm_l13; /* op */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    uint64_t gm_l18; /* c */
    void * gm_l19; /* f */
    uint32_t gm_l20; /* g */
    gm_tiilmfd16v3fc gm_l21; /* eref */
    uint32_t gm_l22; /* op */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_t4lnf71mruqpt gm_l27; /* h */
    void * gm_l28; /* x */
    uint32_t gm_l29; /* id */
    gm_b16_8 gm_l31; /* decls */
    void * gm_l32; /* value */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint32_t gm_l35; /* d */
    void * gm_l37; /* stack */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_b16_8 gm_l41; /* ops */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    uint64_t gm_l44; /* op */
    uint32_t gm_l45; /* c */
    uint64_t gm_l46; /* depth */
    void * gm_l48; /* b */
    gm_b16_8 gm_l51; /* <tmp> */
    uint64_t gm_l52; /* <tmp> */
    gm_trcmmd12cefb1 gm_l53; /* st */
    uint32_t gm_l54; /* local */
    void * gm_l55; /* init */
    uint32_t gm_l56; /* t */
    gm_b16_8 gm_l57; /* <tmp> */
    uint64_t gm_l58; /* <tmp> */
    void * gm_l59; /* c */
    gm_b16_8 gm_l60; /* <tmp> */
    uint64_t gm_l61; /* <tmp> */
    void * gm_l62; /* c */
    GM_STACK_CHECK(&F, 8359u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        void * t6 = gm_l1;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 0));
        uint8_t t8 = gm_f_mentions_error_37knu7r(t5, GM_CONV(gm_csguankko4lmhd, t7));
        if (!t8) goto ie2;
        {
            gm_tgeh6n0c7c8kb t10;
            memset(&t10, 0, sizeof t10);
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
            gm_toabfckqu3onu t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t11 + 8), t10);
            gm_toabfckqu3onu t12 = t11;
            uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        void * t14 = gm_l1;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
        switch (t22) { case ((uint8_t)0ULL): goto ma20; case ((uint8_t)1ULL): goto ma20; case ((uint8_t)2ULL): goto ma20; case ((uint8_t)3ULL): goto ma20; case ((uint8_t)4ULL): goto ma20; case ((uint8_t)5ULL): goto ma20; case ((uint8_t)6ULL): goto ma20; case ((uint8_t)7ULL): goto ma20; case ((uint8_t)8ULL): goto ma20; case ((uint8_t)9ULL): goto ma20; case ((uint8_t)10ULL): goto ma20; case ((uint8_t)11ULL): goto ma20; case ((uint8_t)12ULL): goto ma20; case ((uint8_t)13ULL): goto ma20; case ((uint8_t)14ULL): goto ma20; case ((uint8_t)15ULL): goto ma20; case ((uint8_t)16ULL): goto ma20; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma20; case ((uint8_t)19ULL): goto ma20; case ((uint8_t)20ULL): goto ma20; case ((uint8_t)21ULL): goto ma20; case ((uint8_t)22ULL): goto ma20; case ((uint8_t)23ULL): goto ma20; case ((uint8_t)24ULL): goto ma20; case ((uint8_t)25ULL): goto ma20; case ((uint8_t)26ULL): goto ma20; case ((uint8_t)27ULL): goto ma20; case ((uint8_t)28ULL): goto ma20; case ((uint8_t)29ULL): goto ma20; case ((uint8_t)30ULL): goto ma20; case ((uint8_t)31ULL): goto ma20; case ((uint8_t)32ULL): goto ma20; case ((uint8_t)33ULL): goto ma20; case ((uint8_t)34ULL): goto ma16; case ((uint8_t)35ULL): goto ma20; case ((uint8_t)36ULL): goto ma20; case ((uint8_t)37ULL): goto ma20; case ((uint8_t)38ULL): goto ma20; case ((uint8_t)39ULL): goto ma20; case ((uint8_t)40ULL): goto ma20; case ((uint8_t)41ULL): goto ma20; case ((uint8_t)42ULL): goto ma20; case ((uint8_t)43ULL): goto ma20; case ((uint8_t)44ULL): goto ma20; case ((uint8_t)45ULL): goto ma20; case ((uint8_t)46ULL): goto ma19; case ((uint8_t)47ULL): goto ma20; case ((uint8_t)48ULL): goto ma20; case ((uint8_t)49ULL): goto ma20; case ((uint8_t)50ULL): goto ma20; case ((uint8_t)51ULL): goto ma20; case ((uint8_t)52ULL): goto ma20; case ((uint8_t)53ULL): goto ma20; case ((uint8_t)54ULL): goto ma20; case ((uint8_t)55ULL): goto ma17; case ((uint8_t)56ULL): goto ma20; case ((uint8_t)57ULL): goto ma20; case ((uint8_t)58ULL): goto ma18; default: goto ma20; }
ma16:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
        if (t24 != ((uint8_t)34ULL)) goto mn23;
        gm_tsth9qu1dv0g3 t25 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t14) + 8) + 8) + 0));
        F.l3 = t25;
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((((uint8_t *)t14) + 8) + 8) + 40));
        gm_l4 = t26;
        {
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
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
                        uint64_t t37 = t36;
                        uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&t37));
                        gm_l7 = t40;
                        {
                            uint64_t t42 = gm_l2;
                            void * t43 = gm_l0;
                            uint64_t t44 = gm_l7;
                            uint64_t t45 = (uintptr_t)t44;
                            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_src4tam33qo20j7, 180, 65);
                            void * t47 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t45 * 224);
                            gm_toabfckqu3onu t48;
                            gm_f_stack_expr_33aekgt((void *)&t48, (void *)NULL, cx, NULL, t43, t47);
                            if (gm_unw.kind) {
                                goto gm_ll34;
                            }
                            gm_toabfckqu3onu t49 = t48;
                            if (((((uint8_t *)&t49))[0] != 0)) {
                                gm_tgeh6n0c7c8kb t50 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t49) + 8));
                                gm_toabfckqu3onu t51;
                                GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                                ((uint8_t *)&t51)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t51 + 8), t50);
                                F.ret = t51;
                                goto gm_fret;
                            }
                            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&t49) + 8));
                            uint64_t t53 = (t42 > t52 ? t42 : t52);
                            gm_l2 = t53;
                        }
                        goto me38;
mn39:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 180, 13);
me38:;
                    }
lc30:;
                    {
                        uint64_t t55 = gm_l6;
                        uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                        uint64_t t58 = r56;
                        gm_l6 = t58;
                    }
                }
gm_ll34:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb30; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc30; }
                }
                goto gm_lad;
lb30:;
            }
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            switch (t64) { case ((uint8_t)0ULL): goto ma60; case ((uint8_t)1ULL): goto ma62; case ((uint8_t)2ULL): goto ma61; default: goto ma63; }
ma60:;
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t66 != ((uint8_t)0ULL)) goto mn65;
            uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 8));
            gm_l9 = t67;
            {
                void * t69 = gm_l0;
                uint32_t t70 = gm_l9;
                gm_b16_8 t71 = gm_l4;
                gm_b16_8 t72 = t71;
                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                void * t74 = gm_l0;
                gm_b16_8 t75 = GM_LD(gm_b16_8, ((((uint8_t *)t74) + 48) + 0));
                gm_b16_8 t76 = t75;
                gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                void * t78 = gm_l1;
                gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t78) + 208));
                gm_toabfckqu3onu t80;
                gm_f_stack_call_poqmlno((void *)&t80, (void *)NULL, cx, NULL, t69, t70, t73, t77, t79);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_toabfckqu3onu t81 = t80;
                if (((((uint8_t *)&t81))[0] != 0)) {
                    gm_tgeh6n0c7c8kb t82 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t81) + 8));
                    gm_toabfckqu3onu t83;
                    GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                    ((uint8_t *)&t83)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t83 + 8), t82);
                    F.ret = t83;
                    goto gm_fret;
                }
                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&t81) + 8));
                gm_l8 = t84;
            }
            goto me59;
mn65:;
ma61:;
            uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t86 != ((uint8_t)2ULL)) goto mn85;
            void * t87 = GM_LD(void *, (((uint8_t *)&F.l3) + 8));
            gm_l10 = t87;
            {
                uint64_t t89 = gm_l2;
                void * t90 = gm_l0;
                void * t91 = gm_l10;
                gm_toabfckqu3onu t92;
                gm_f_stack_expr_33aekgt((void *)&t92, (void *)NULL, cx, NULL, t90, t91);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_toabfckqu3onu t93 = t92;
                if (((((uint8_t *)&t93))[0] != 0)) {
                    gm_tgeh6n0c7c8kb t94 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t93) + 8));
                    gm_toabfckqu3onu t95;
                    GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
                    ((uint8_t *)&t95)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t95 + 8), t94);
                    F.ret = t95;
                    goto gm_fret;
                }
                uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&t93) + 8));
                uint64_t t97 = (t89 > t96 ? t89 : t96);
                gm_l2 = t97;
            }
            {
                void * t98 = gm_l0;
                void * t99 = gm_l10;
                gm_t9805qtonbsag t100 = gm_f_stack_target_7vr78v6(t98, t99);
                gm_t9805qtonbsag t101 = t100;
                uint32_t t102;
                if (((((uint8_t *)&t101))[4] != 0)) {
                    uint32_t t103 = GM_LD(uint32_t, (((uint8_t *)&t101) + 0));
                    t102 = t103;
                } else {
                    gm_tbgd14esf0jvi t105;
                    memset((uint8_t *)&t105 + 12, 0, 4);
                    void * t106 = gm_l1;
                    gm_tl874t2atnlh3 t107 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t106) + 208));
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t105 + 0, t107);
                    gm_b16_8 t108;
                    GM_ST(void *, (uint8_t *)&t108, (void *)(gm_sg0997vvtiund));
                    GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(64));
                    GM_ST(gm_b16_8, (uint8_t *)&t105 + 16, t108);
                    gm_tgeh6n0c7c8kb t109;
                    GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t109 + 8), t105);
                    gm_toabfckqu3onu t110;
                    GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
                    ((uint8_t *)&t110)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t110 + 8), t109);
                    gm_toabfckqu3onu t111 = t110;
                    uint8_t t112 = ((((uint8_t *)&t111))[0] != 0);
                    F.ret = t110;
                    goto gm_fret;
                }
                gm_l11 = t102;
            }
            {
                void * t113 = gm_l0;
                uint32_t t114 = gm_l11;
                gm_b16_8 t115 = gm_l4;
                gm_b16_8 t116 = t115;
                gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
                void * t118 = gm_l0;
                gm_b16_8 t119 = GM_LD(gm_b16_8, ((((uint8_t *)t118) + 48) + 0));
                gm_b16_8 t120 = t119;
                gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                void * t122 = gm_l1;
                gm_tl874t2atnlh3 t123 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t122) + 208));
                gm_toabfckqu3onu t124;
                gm_f_stack_call_poqmlno((void *)&t124, (void *)NULL, cx, NULL, t113, t114, t117, t121, t123);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_toabfckqu3onu t125 = t124;
                if (((((uint8_t *)&t125))[0] != 0)) {
                    gm_tgeh6n0c7c8kb t126 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t125) + 8));
                    gm_toabfckqu3onu t127;
                    GM_ST(uint8_t, (uint8_t *)&t127 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t127 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t127 + 4, 0);
                    ((uint8_t *)&t127)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t127 + 8), t126);
                    F.ret = t127;
                    goto gm_fret;
                }
                uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&t125) + 8));
                gm_l8 = t128;
            }
            goto me59;
mn85:;
ma62:;
            uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t130 != ((uint8_t)1ULL)) goto mn129;
            gm_tiilmfd16v3fc t131 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)&F.l3) + 8) + 0));
            gm_l12 = t131;
            uint32_t t132 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 8) + 24));
            gm_l13 = t132;
            {
                void * t134 = gm_l0;
                void * t135 = (void *)((uint8_t *)&gm_l12);
                uint32_t t136 = gm_l13;
                gm_b16_8 t137 = gm_l4;
                gm_b16_8 t138 = t137;
                gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&t138));
                void * t140 = gm_l1;
                gm_tl874t2atnlh3 t141 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t140) + 208));
                gm_toabfckqu3onu t142;
                gm_f_stack_perform_soivemm((void *)&t142, (void *)NULL, cx, NULL, t134, t135, t136, t139, t141);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_toabfckqu3onu t143 = t142;
                if (((((uint8_t *)&t143))[0] != 0)) {
                    gm_tgeh6n0c7c8kb t144 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t143) + 8));
                    gm_toabfckqu3onu t145;
                    GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t145 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
                    ((uint8_t *)&t145)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t145 + 8), t144);
                    F.ret = t145;
                    goto gm_fret;
                }
                uint64_t t146 = GM_LD(uint64_t, (((uint8_t *)&t143) + 8));
                gm_l8 = t146;
            }
            goto me59;
mn129:;
ma63:;
            gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 182, 13);
me59:;
        }
        {
            uint64_t t147 = gm_l2;
            uint64_t t148 = gm_l8;
            uint64_t t149 = (t147 > t148 ? t147 : t148);
            gm_l2 = t149;
        }
        goto me15;
mn23:;
ma17:;
        uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
        if (t151 != ((uint8_t)55ULL)) goto mn150;
        gm_tntn2tf15puib t152 = GM_LD(gm_tntn2tf15puib, ((((uint8_t *)t14) + 8) + 8));
        F.l14 = t152;
        {
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                uint64_t t155 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 72) + 8);
                gm_l15 = t155;
            }
            {
                for (;;) {
                    uint64_t t157 = gm_l16;
                    uint64_t t158 = gm_l15;
                    uint8_t t159 = (t157 < t158);
                    if (!t159) goto lb156;
                    {
                        uint64_t t162 = gm_l16;
                        uint64_t t163 = t162;
                        uint64_t t166 = GM_LD(uint64_t, ((uint8_t *)&t163));
                        gm_l17 = t166;
                        {
                            uint64_t t168 = gm_l2;
                            void * t169 = gm_l0;
                            uint64_t t170 = gm_l17;
                            uint64_t t171 = (uintptr_t)t170;
                            uint64_t t172 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 72) + 8);
                            if (t171 >= (uintptr_t)(t172)) gm_trap_index(t171, t172, gm_src4tam33qo20j7, 196, 67);
                            void * t173 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 72)) + t171 * 224);
                            gm_toabfckqu3onu t174;
                            gm_f_stack_expr_33aekgt((void *)&t174, (void *)NULL, cx, NULL, t169, t173);
                            if (gm_unw.kind) {
                                goto gm_ll160;
                            }
                            gm_toabfckqu3onu t175 = t174;
                            if (((((uint8_t *)&t175))[0] != 0)) {
                                gm_tgeh6n0c7c8kb t176 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t175) + 8));
                                gm_toabfckqu3onu t177;
                                GM_ST(uint8_t, (uint8_t *)&t177 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t177 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t177 + 4, 0);
                                ((uint8_t *)&t177)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t177 + 8), t176);
                                F.ret = t177;
                                goto gm_fret;
                            }
                            uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)&t175) + 8));
                            uint64_t t179 = (t168 > t178 ? t168 : t178);
                            gm_l2 = t179;
                        }
                        goto me164;
mn165:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 196, 13);
me164:;
                    }
lc156:;
                    {
                        uint64_t t181 = gm_l16;
                        uint64_t r182; bool o183 = __builtin_add_overflow(t181, ((uint64_t)1ULL), &r182);
                        uint64_t t184 = r182;
                        gm_l16 = t184;
                    }
                }
gm_ll160:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb156; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc156; }
                }
                goto gm_lad;
lb156:;
            }
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            uint8_t t187 = GM_LD(uint8_t, ((((uint8_t *)&F.l14) + 0) + 0));
            if (t187 != ((uint8_t)1ULL)) goto mn186;
            void * t188 = GM_LD(void *, ((((uint8_t *)&F.l14) + 0) + 8));
            gm_l19 = t188;
            {
                uint64_t t190 = gm_l2;
                void * t191 = gm_l0;
                void * t192 = gm_l19;
                gm_toabfckqu3onu t193;
                gm_f_stack_expr_33aekgt((void *)&t193, (void *)NULL, cx, NULL, t191, t192);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_toabfckqu3onu t194 = t193;
                if (((((uint8_t *)&t194))[0] != 0)) {
                    gm_tgeh6n0c7c8kb t195 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t194) + 8));
                    gm_toabfckqu3onu t196;
                    GM_ST(uint8_t, (uint8_t *)&t196 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t196 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t196 + 4, 0);
                    ((uint8_t *)&t196)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t196 + 8), t195);
                    F.ret = t196;
                    goto gm_fret;
                }
                uint64_t t197 = GM_LD(uint64_t, (((uint8_t *)&t194) + 8));
                uint64_t t198 = (t190 > t197 ? t190 : t197);
                gm_l2 = t198;
            }
            {
                void * t199 = gm_l0;
                void * t200 = gm_l19;
                gm_t9805qtonbsag t201 = gm_f_stack_target_7vr78v6(t199, t200);
                gm_t9805qtonbsag t202 = t201;
                uint32_t t203;
                if (((((uint8_t *)&t202))[4] != 0)) {
                    uint32_t t204 = GM_LD(uint32_t, (((uint8_t *)&t202) + 0));
                    t203 = t204;
                } else {
                    gm_tbgd14esf0jvi t206;
                    memset((uint8_t *)&t206 + 12, 0, 4);
                    void * t207 = gm_l1;
                    gm_tl874t2atnlh3 t208 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t207) + 208));
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t206 + 0, t208);
                    gm_b16_8 t209;
                    GM_ST(void *, (uint8_t *)&t209, (void *)(gm_s2sodn6uclrlt));
                    GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(62));
                    GM_ST(gm_b16_8, (uint8_t *)&t206 + 16, t209);
                    gm_tgeh6n0c7c8kb t210;
                    GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t210 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t210 + 8), t206);
                    gm_toabfckqu3onu t211;
                    GM_ST(uint8_t, (uint8_t *)&t211 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t211 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t211 + 4, 0);
                    ((uint8_t *)&t211)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t211 + 8), t210);
                    gm_toabfckqu3onu t212 = t211;
                    uint8_t t213 = ((((uint8_t *)&t212))[0] != 0);
                    F.ret = t211;
                    goto gm_fret;
                }
                gm_l20 = t203;
            }
            {
                void * t214 = gm_l0;
                uint32_t t215 = gm_l20;
                gm_b16_8 t216 = gm_f_empty_slice_g3tddrb_2();
                gm_b16_8 t217 = t216;
                gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&t217));
                void * t219 = gm_l0;
                gm_b16_8 t220 = GM_LD(gm_b16_8, ((((uint8_t *)t219) + 48) + 0));
                gm_b16_8 t221 = t220;
                gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&t221));
                void * t223 = gm_l1;
                gm_tl874t2atnlh3 t224 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t223) + 208));
                gm_toabfckqu3onu t225;
                gm_f_stack_call_poqmlno((void *)&t225, (void *)NULL, cx, NULL, t214, t215, t218, t222, t224);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_toabfckqu3onu t226 = t225;
                if (((((uint8_t *)&t226))[0] != 0)) {
                    gm_tgeh6n0c7c8kb t227 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t226) + 8));
                    gm_toabfckqu3onu t228;
                    GM_ST(uint8_t, (uint8_t *)&t228 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t228 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t228 + 4, 0);
                    ((uint8_t *)&t228)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t228 + 8), t227);
                    F.ret = t228;
                    goto gm_fret;
                }
                uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)&t226) + 8));
                gm_l18 = t229;
            }
            goto me185;
mn186:;
            uint8_t t231 = GM_LD(uint8_t, ((((uint8_t *)&F.l14) + 0) + 0));
            if (t231 != ((uint8_t)0ULL)) goto mn230;
            gm_tiilmfd16v3fc t232 = GM_LD(gm_tiilmfd16v3fc, (((((uint8_t *)&F.l14) + 0) + 8) + 0));
            gm_l21 = t232;
            uint32_t t233 = GM_LD(uint32_t, (((((uint8_t *)&F.l14) + 0) + 8) + 24));
            gm_l22 = t233;
            {
                void * t235 = gm_l0;
                void * t236 = (void *)((uint8_t *)&gm_l21);
                uint32_t t237 = gm_l22;
                gm_b16_8 t238 = gm_f_empty_slice_g3tddrb_2();
                gm_b16_8 t239 = t238;
                gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)&t239));
                void * t241 = gm_l1;
                gm_tl874t2atnlh3 t242 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t241) + 208));
                gm_toabfckqu3onu t243;
                gm_f_stack_perform_soivemm((void *)&t243, (void *)NULL, cx, NULL, t235, t236, t237, t240, t242);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_toabfckqu3onu t244 = t243;
                if (((((uint8_t *)&t244))[0] != 0)) {
                    gm_tgeh6n0c7c8kb t245 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t244) + 8));
                    gm_toabfckqu3onu t246;
                    GM_ST(uint8_t, (uint8_t *)&t246 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t246 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t246 + 4, 0);
                    ((uint8_t *)&t246)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t246 + 8), t245);
                    F.ret = t246;
                    goto gm_fret;
                }
                uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&t244) + 8));
                gm_l18 = t247;
            }
            goto me185;
mn230:;
            gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 198, 13);
me185:;
        }
        {
            uint64_t t248 = gm_l2;
            uint64_t t249 = gm_l18;
            uint64_t t250 = (t248 > t249 ? t248 : t249);
            gm_l2 = t250;
        }
        goto me15;
mn150:;
ma18:;
        uint8_t t252 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
        if (t252 != ((uint8_t)58ULL)) goto mn251;
        gm_t710lsrq1m8gk t253 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t14) + 8) + 8));
        F.l23 = t253;
        {
            gm_f_list_o00nc4s((void *)&F.l24, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 8));
                gm_l25 = t256;
            }
            {
                gm_l26 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t258 = gm_l26;
                    uint64_t t259 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                    uint8_t t260 = (t258 < t259);
                    if (!t260) goto lb257;
                    {
                        uint64_t t263 = gm_l26;
                        uint64_t t264 = (uintptr_t)t263;
                        uint64_t t265 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                        gm_t4lnf71mruqpt t268 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t264 * 32));
                        gm_l27 = t268;
                        {
                            uint8_t t272 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                            if (t272 != ((uint8_t)0ULL)) goto mn271;
                            void * t273 = GM_LD(void *, (((uint8_t *)&gm_l27) + 8));
                            gm_l28 = t273;
                            {
                                uint64_t t275 = gm_l2;
                                void * t276 = gm_l0;
                                void * t277 = gm_l28;
                                gm_toabfckqu3onu t278;
                                gm_f_stack_expr_33aekgt((void *)&t278, (void *)NULL, cx, NULL, t276, t277);
                                if (gm_unw.kind) {
                                    goto gm_ll261;
                                }
                                gm_toabfckqu3onu t279 = t278;
                                if (((((uint8_t *)&t279))[0] != 0)) {
                                    gm_tgeh6n0c7c8kb t280 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t279) + 8));
                                    gm_toabfckqu3onu t281;
                                    GM_ST(uint8_t, (uint8_t *)&t281 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t281 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t281 + 4, 0);
                                    ((uint8_t *)&t281)[0] = 1;
                                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t281 + 8), t280);
                                    F.ret = t281;
                                    goto gm_fret;
                                }
                                uint64_t t282 = GM_LD(uint64_t, (((uint8_t *)&t279) + 8));
                                uint64_t t283 = (t275 > t282 ? t275 : t282);
                                gm_l2 = t283;
                            }
                            {
                                void * t285 = gm_l0;
                                void * t286 = GM_LD(void *, (((uint8_t *)t285) + 0));
                                void * t287 = (void *)(((uint8_t *)t286) + 8);
                                void * t288 = gm_l28;
                                gm_t88rcucg1b7uk t289 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t288) + 0));
                                gm_t53at80pfdus3 t290;
                                gm_f_kind_jcnt4hm((void *)&t290, t287, GM_CONV(gm_csguankko4lmhd, t289));
                                gm_t53at80pfdus3 t291 = t290;
                                uint8_t t292 = GM_LD(uint8_t, (((uint8_t *)&t291) + 0));
                                if (t292 != ((uint8_t)25ULL)) goto ie284;
                                uint32_t t293 = GM_LD(uint32_t, (((uint8_t *)&t291) + 8));
                                gm_l29 = t293;
                                {
                                    uint32_t t295 = gm_l29;
                                    uint64_t t296 = (uint64_t)t295;
                                    uint64_t t297 = (uintptr_t)t296;
                                    void * t298 = gm_l0;
                                    void * t299 = GM_LD(void *, (((uint8_t *)t298) + 0));
                                    uint64_t t300 = GM_LD(uintptr_t, ((((uint8_t *)t299) + 504) + 0) + 8);
                                    if (t297 >= (uintptr_t)(t300)) gm_trap_index(t297, t300, gm_src4tam33qo20j7, 217, 32);
                                    gm_t1cs2fjnrqap1 t301 = GM_LD(gm_t1cs2fjnrqap1, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t299) + 504) + 0)) + t297 * 40));
                                    F.l30 = t301;
                                }
                                {
                                    void * t302 = (void *)((uint8_t *)&F.l24);
                                    gm_t1cs2fjnrqap1 t303;
                                    memcpy((uint8_t *)&t303 + 0, (((uint8_t *)&F.l30) + 0), 24);
                                    gm_b16_8 t304 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 24));
                                    GM_ST(gm_b16_8, (uint8_t *)&t303 + 24, t304);
                                    gm_tftb1odc739t5 t305 = gm_f_push_njfep1h(t302, &t303);
                                    gm_tftb1odc739t5 t306 = t305;
                                    if (((((uint8_t *)&t306))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t306) + 1, gm_src4tam33qo20j7, 218, 25);
                                    gm_b0_1 t307 = GM_LD(gm_b0_1, (((uint8_t *)&t306) + 1));
                                }
                                goto ix284;
ie284:;
                                {
                                    gm_tgeh6n0c7c8kb t309;
                                    memset(&t309, 0, sizeof t309);
                                    GM_ST(uint8_t, ((uint8_t *)&t309 + 0), ((uint8_t)0ULL));
                                    gm_toabfckqu3onu t310;
                                    GM_ST(uint8_t, (uint8_t *)&t310 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t310 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t310 + 4, 0);
                                    ((uint8_t *)&t310)[0] = 1;
                                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t310 + 8), t309);
                                    gm_toabfckqu3onu t311 = t310;
                                    uint8_t t312 = ((((uint8_t *)&t311))[0] != 0);
                                    F.ret = t310;
                                    goto gm_fret;
                                }
ix284:;
                            }
                            goto me270;
mn271:;
                            uint8_t t314 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                            if (t314 != ((uint8_t)1ULL)) goto mn313;
                            gm_b16_8 t315 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l27) + 8) + 0));
                            gm_l31 = t315;
                            void * t316 = GM_LD(void *, ((((uint8_t *)&gm_l27) + 8) + 16));
                            gm_l32 = t316;
                            {
                                uint64_t t318 = gm_l2;
                                void * t319 = gm_l0;
                                void * t320 = gm_l32;
                                gm_toabfckqu3onu t321;
                                gm_f_stack_expr_33aekgt((void *)&t321, (void *)NULL, cx, NULL, t319, t320);
                                if (gm_unw.kind) {
                                    goto gm_ll261;
                                }
                                gm_toabfckqu3onu t322 = t321;
                                if (((((uint8_t *)&t322))[0] != 0)) {
                                    gm_tgeh6n0c7c8kb t323 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t322) + 8));
                                    gm_toabfckqu3onu t324;
                                    GM_ST(uint8_t, (uint8_t *)&t324 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t324 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t324 + 4, 0);
                                    ((uint8_t *)&t324)[0] = 1;
                                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t324 + 8), t323);
                                    F.ret = t324;
                                    goto gm_fret;
                                }
                                uint64_t t325 = GM_LD(uint64_t, (((uint8_t *)&t322) + 8));
                                uint64_t t326 = (t318 > t325 ? t318 : t325);
                                gm_l2 = t326;
                            }
                            {
                                {
                                    gm_b16_8 t328 = gm_l31;
                                    gm_l33 = t328;
                                }
                                {
                                    gm_l34 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t330 = gm_l34;
                                        uint64_t t331 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                        uint8_t t332 = (t330 < t331);
                                        if (!t332) goto lb329;
                                        {
                                            uint64_t t335 = gm_l34;
                                            uint64_t t336 = (uintptr_t)t335;
                                            uint64_t t337 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                            uint32_t t338 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t336 * 4));
                                            uint32_t t339 = t338;
                                            uint32_t t342 = GM_LD(uint32_t, ((uint8_t *)&t339));
                                            gm_l35 = t342;
                                            {
                                                uint32_t t344 = gm_l35;
                                                uint64_t t345 = (uint64_t)t344;
                                                uint64_t t346 = (uintptr_t)t345;
                                                void * t347 = gm_l0;
                                                void * t348 = GM_LD(void *, (((uint8_t *)t347) + 0));
                                                uint64_t t349 = GM_LD(uintptr_t, ((((uint8_t *)t348) + 544) + 0) + 8);
                                                if (t346 >= (uintptr_t)(t349)) gm_trap_index(t346, t349, gm_src4tam33qo20j7, 223, 37);
                                                gm_tvc83pqnepc2e t350 = GM_LD(gm_tvc83pqnepc2e, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t348) + 544) + 0)) + t346 * 64));
                                                F.l36 = t350;
                                            }
                                            {
                                                void * t351 = (void *)((uint8_t *)&F.l24);
                                                gm_t1cs2fjnrqap1 t352;
                                                memcpy((uint8_t *)&t352 + 0, (((uint8_t *)&F.l36) + 0), 24);
                                                gm_b16_8 t353 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 48));
                                                GM_ST(gm_b16_8, (uint8_t *)&t352 + 24, t353);
                                                gm_tftb1odc739t5 t354 = gm_f_push_njfep1h(t351, &t352);
                                                gm_tftb1odc739t5 t355 = t354;
                                                if (((((uint8_t *)&t355))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t355) + 1, gm_src4tam33qo20j7, 224, 29);
                                                gm_b0_1 t356 = GM_LD(gm_b0_1, (((uint8_t *)&t355) + 1));
                                            }
                                            goto me340;
mn341:;
                                            gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 222, 25);
me340:;
                                        }
lc329:;
                                        {
                                            uint64_t t358 = gm_l34;
                                            uint64_t r359; bool o360 = __builtin_add_overflow(t358, ((uint64_t)1ULL), &r359);
                                            uint64_t t361 = r359;
                                            gm_l34 = t361;
                                        }
                                    }
lb329:;
                                }
                            }
                            goto me270;
mn313:;
                            gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 213, 17);
me270:;
                        }
                        goto me266;
mn267:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 212, 13);
me266:;
                    }
lc257:;
                    {
                        uint64_t t363 = gm_l26;
                        uint64_t r364; bool o365 = __builtin_add_overflow(t363, ((uint64_t)1ULL), &r364);
                        uint64_t t366 = r364;
                        gm_l26 = t366;
                    }
                }
gm_ll261:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb257; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc257; }
                }
                goto gm_lad;
lb257:;
            }
        }
        {
            void * t368 = GM_LD(void *, (((uint8_t *)&F.l23) + 24));
            void * t369 = t368;
            if (!(GM_LD(void *, ((uint8_t *)&t369)) != 0)) goto ie367;
            void * t370 = GM_LD(void *, ((uint8_t *)&t369));
            gm_l37 = t370;
            {
                uint64_t t372 = gm_l2;
                void * t373 = gm_l0;
                void * t374 = gm_l37;
                gm_toabfckqu3onu t375;
                gm_f_stack_expr_33aekgt((void *)&t375, (void *)NULL, cx, NULL, t373, t374);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_toabfckqu3onu t376 = t375;
                if (((((uint8_t *)&t376))[0] != 0)) {
                    gm_tgeh6n0c7c8kb t377 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t376) + 8));
                    gm_toabfckqu3onu t378;
                    GM_ST(uint8_t, (uint8_t *)&t378 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t378 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t378 + 4, 0);
                    ((uint8_t *)&t378)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t378 + 8), t377);
                    F.ret = t378;
                    goto gm_fret;
                }
                uint64_t t379 = GM_LD(uint64_t, (((uint8_t *)&t376) + 8));
                uint64_t t380 = (t372 > t379 ? t372 : t379);
                gm_l2 = t380;
            }
            {
                {
                    gm_b16_8 t382 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                    gm_l38 = t382;
                }
                {
                    gm_l39 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t384 = gm_l39;
                        uint64_t t385 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                        uint8_t t386 = (t384 < t385);
                        if (!t386) goto lb383;
                        {
                            uint64_t t389 = gm_l39;
                            uint64_t t390 = (uintptr_t)t389;
                            uint64_t t391 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                            gm_t1cs2fjnrqap1 t394 = GM_LD(gm_t1cs2fjnrqap1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t390 * 40));
                            F.l40 = t394;
                            {
                                uint32_t t396 = GM_LD(uint32_t, ((((uint8_t *)&F.l40) + 0) + 0));
                                uint64_t t397 = (uint64_t)t396;
                                uint64_t t398 = (uintptr_t)t397;
                                void * t399 = gm_l0;
                                void * t400 = GM_LD(void *, (((uint8_t *)t399) + 0));
                                uint64_t t401 = GM_LD(uintptr_t, ((((uint8_t *)t400) + 1048) + 0) + 8);
                                if (t398 >= (uintptr_t)(t401)) gm_trap_index(t398, t401, gm_src4tam33qo20j7, 234, 28);
                                gm_b16_8 t402 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t400) + 1048) + 0)) + t398 * 56) + 40));
                                gm_l41 = t402;
                            }
                            {
                                {
                                    gm_b16_8 t404 = GM_LD(gm_b16_8, (((uint8_t *)&F.l40) + 24));
                                    gm_b16_8 t405 = t404;
                                    gm_b16_8 t406 = GM_LD(gm_b16_8, ((uint8_t *)&t405));
                                    gm_l42 = t406;
                                }
                                {
                                    gm_l43 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t408 = gm_l43;
                                        uint64_t t409 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                        uint8_t t410 = (t408 < t409);
                                        if (!t410) goto lb407;
                                        {
                                            uint64_t t413 = gm_l43;
                                            uint64_t t414 = gm_l43;
                                            uint64_t t415 = (uintptr_t)t414;
                                            uint64_t t416 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                            uint32_t t417 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t415 * 4));
                                            gm_tn68el8d14vku t418;
                                            GM_ST(uint32_t, (uint8_t *)&t418 + 12, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&t418 + 0), t413);
                                            GM_ST(uint32_t, ((uint8_t *)&t418 + 8), t417);
                                            gm_tn68el8d14vku t419 = t418;
                                            uint64_t t422 = GM_LD(uint64_t, (((uint8_t *)&t419) + 0));
                                            gm_l44 = t422;
                                            uint32_t t423 = GM_LD(uint32_t, (((uint8_t *)&t419) + 8));
                                            gm_l45 = t423;
                                            {
                                                uint64_t t426 = gm_l44;
                                                uint64_t t427 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                                                uint8_t t428 = (t426 < t427);
                                                if (!t428) goto ie425;
                                                uint64_t t429 = gm_l44;
                                                uint64_t t430 = (uintptr_t)t429;
                                                uint64_t t431 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                                                if (t430 >= (uintptr_t)(t431)) gm_trap_index(t430, t431, gm_src4tam33qo20j7, 236, 45);
                                                uint8_t t432 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l41)) + t430 * 48) + 16) + 0) == ((uint8_t)2ULL)));
                                                if (!t432) goto ie425;
                                                {
                                                    uint64_t t434 = gm_l2;
                                                    void * t435 = gm_l0;
                                                    uint32_t t436 = gm_l45;
                                                    gm_b16_8 t437 = gm_f_empty_slice_g3tddrb_2();
                                                    gm_b16_8 t438 = t437;
                                                    gm_b16_8 t439 = GM_LD(gm_b16_8, ((uint8_t *)&t438));
                                                    void * t440 = gm_l0;
                                                    gm_b16_8 t441 = GM_LD(gm_b16_8, ((((uint8_t *)t440) + 48) + 0));
                                                    gm_b16_8 t442 = t441;
                                                    gm_b16_8 t443 = GM_LD(gm_b16_8, ((uint8_t *)&t442));
                                                    void * t444 = gm_l1;
                                                    gm_tl874t2atnlh3 t445 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t444) + 208));
                                                    gm_toabfckqu3onu t446;
                                                    gm_f_stack_call_poqmlno((void *)&t446, (void *)NULL, cx, NULL, t435, t436, t439, t443, t445);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll411;
                                                    }
                                                    gm_toabfckqu3onu t447 = t446;
                                                    if (((((uint8_t *)&t447))[0] != 0)) {
                                                        gm_tgeh6n0c7c8kb t448 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t447) + 8));
                                                        gm_toabfckqu3onu t449;
                                                        GM_ST(uint8_t, (uint8_t *)&t449 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t449 + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t449 + 4, 0);
                                                        ((uint8_t *)&t449)[0] = 1;
                                                        GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t449 + 8), t448);
                                                        F.ret = t449;
                                                        goto gm_fret;
                                                    }
                                                    uint64_t t450 = GM_LD(uint64_t, (((uint8_t *)&t447) + 8));
                                                    uint64_t t451 = (t434 > t450 ? t434 : t450);
                                                    gm_l2 = t451;
                                                }
                                                goto ix425;
ie425:;
ix425:;
                                            }
                                            goto me420;
mn421:;
                                            gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 235, 21);
me420:;
                                        }
lc407:;
                                        {
                                            uint64_t t453 = gm_l43;
                                            uint64_t r454; bool o455 = __builtin_add_overflow(t453, ((uint64_t)1ULL), &r454);
                                            uint64_t t456 = r454;
                                            gm_l43 = t456;
                                        }
                                    }
gm_ll411:;
                                    if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb407; }
                                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc407; }
                                    }
                                    goto gm_ll387;
lb407:;
                                }
                            }
                            goto me392;
mn393:;
                            gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 233, 17);
me392:;
                        }
lc383:;
                        {
                            uint64_t t458 = gm_l39;
                            uint64_t r459; bool o460 = __builtin_add_overflow(t458, ((uint64_t)1ULL), &r459);
                            uint64_t t461 = r459;
                            gm_l39 = t461;
                        }
                    }
gm_ll387:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb383; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc383; }
                    }
                    goto gm_lad;
lb383:;
                }
            }
            goto ix367;
ie367:;
            {
                void * t463 = gm_l0;
                uint64_t t464 = GM_LD(uintptr_t, ((((uint8_t *)t463) + 48) + 0) + 8);
                gm_l46 = t464;
            }
            {
                void * t465 = gm_l0;
                void * t466 = (void *)(((uint8_t *)t465) + 48);
                gm_b16_8 t467 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                gm_b16_8 t468 = t467;
                gm_b16_8 t469 = GM_LD(gm_b16_8, ((uint8_t *)&t468));
                gm_tftb1odc739t5 t470 = gm_f_extend_2kurgou(t466, t469);
                gm_tftb1odc739t5 t471 = t470;
                if (((((uint8_t *)&t471))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t471) + 1, gm_src4tam33qo20j7, 241, 17);
                gm_b0_1 t472 = GM_LD(gm_b0_1, (((uint8_t *)&t471) + 1));
            }
            {
                void * t473 = gm_l0;
                void * t474 = GM_LD(void *, (((uint8_t *)&F.l23) + 32));
                gm_f_stack_expr_33aekgt((void *)&F.l47, (void *)NULL, cx, NULL, t473, t474);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t475 = gm_l0;
                void * t476 = (void *)(((uint8_t *)t475) + 48);
                uint64_t t477 = gm_l46;
                gm_f_truncate_31h1fac(t476, t477);
            }
            {
                uint64_t t478 = gm_l2;
                gm_toabfckqu3onu t479 = F.l47;
                gm_toabfckqu3onu t480 = t479;
                if (((((uint8_t *)&t480))[0] != 0)) {
                    gm_tgeh6n0c7c8kb t481 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t480) + 8));
                    gm_toabfckqu3onu t482;
                    GM_ST(uint8_t, (uint8_t *)&t482 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t482 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t482 + 4, 0);
                    ((uint8_t *)&t482)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t482 + 8), t481);
                    F.ret = t482;
                    goto gm_fret;
                }
                uint64_t t483 = GM_LD(uint64_t, (((uint8_t *)&t480) + 8));
                uint64_t t484 = (t478 > t483 ? t478 : t483);
                gm_l2 = t484;
            }
ix367:;
        }
        goto me15;
mn251:;
ma19:;
        uint8_t t486 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
        if (t486 != ((uint8_t)46ULL)) goto mn485;
        void * t487 = GM_LD(void *, ((((uint8_t *)t14) + 8) + 8));
        gm_l48 = t487;
        {
            void * t489 = gm_l0;
            gm_b16_8 t490 = GM_LD(gm_b16_8, ((((uint8_t *)t489) + 8) + 0));
            gm_b16_8 t491 = t490;
            void * t492 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t491)));
            uint64_t t493 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t491) + 8);
            uint64_t t494 = (t493 - 1);
            gm_tijr008e33o2m t495;
            if (t494 < t493) {
            gm_tha5675rv512q t496 = GM_LD(gm_tha5675rv512q, ((uint8_t *)t492 + t494 * 48));
            gm_tijr008e33o2m t497;
            GM_ST(uint8_t, (uint8_t *)&t497 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t497 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t497 + 52, 0);
            GM_ST(gm_tha5675rv512q, ((uint8_t *)&t497), t496);
            ((uint8_t *)&t497)[48] = 1;
                t495 = t497;
            } else {
            gm_tijr008e33o2m t498;
            memset(&t498, 0, sizeof t498);
                t495 = t498;
            }
            gm_tijr008e33o2m t499 = t495;
            gm_tha5675rv512q t500;
            if (((((uint8_t *)&t499))[48] != 0)) {
                gm_tha5675rv512q t501 = GM_LD(gm_tha5675rv512q, (((uint8_t *)&t499) + 0));
                t500 = t501;
            } else {
                gm_tgeh6n0c7c8kb t502;
                memset(&t502, 0, sizeof t502);
                GM_ST(uint8_t, ((uint8_t *)&t502 + 0), ((uint8_t)0ULL));
                gm_toabfckqu3onu t503;
                GM_ST(uint8_t, (uint8_t *)&t503 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t503 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t503 + 4, 0);
                ((uint8_t *)&t503)[0] = 1;
                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t503 + 8), t502);
                gm_toabfckqu3onu t504 = t503;
                uint8_t t505 = ((((uint8_t *)&t504))[0] != 0);
                F.ret = t503;
                goto gm_fret;
            }
            F.l49 = t500;
        }
        {
            void * t506 = gm_l0;
            void * t507 = GM_LD(void *, (((uint8_t *)t506) + 0));
            uint32_t t508 = GM_LD(uint32_t, (((uint8_t *)&F.l49) + 0));
            gm_f_def_of_m512sbu((void *)&F.l50, t507, t508);
        }
        {
            {
                void * t510 = gm_l48;
                gm_b16_8 t511 = GM_LD(gm_b16_8, (((uint8_t *)t510) + 0));
                gm_l51 = t511;
            }
            {
                gm_l52 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t513 = gm_l52;
                    uint64_t t514 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                    uint8_t t515 = (t513 < t514);
                    if (!t515) goto lb512;
                    {
                        uint64_t t518 = gm_l52;
                        uint64_t t519 = (uintptr_t)t518;
                        uint64_t t520 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                        gm_trcmmd12cefb1 t523 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l51)) + t519 * 32));
                        gm_l53 = t523;
                        {
                            uint8_t t526 = GM_LD(uint8_t, (((uint8_t *)&gm_l53) + 0));
                            if (t526 != ((uint8_t)0ULL)) goto ie525;
                            uint32_t t527 = GM_LD(uint32_t, ((((uint8_t *)&gm_l53) + 8) + 0));
                            gm_l54 = t527;
                            if (!(GM_LD(void *, ((((uint8_t *)&gm_l53) + 8) + 8)) != 0)) goto ie525;
                            void * t528 = GM_LD(void *, ((((uint8_t *)&gm_l53) + 8) + 8));
                            gm_l55 = t528;
                            {
                                uint32_t t531 = gm_l54;
                                uint64_t t532 = (uint64_t)t531;
                                uint64_t t533 = (uintptr_t)t532;
                                uint64_t t534 = GM_LD(uintptr_t, (((uint8_t *)&F.l50) + 24) + 8);
                                if (t533 >= (uintptr_t)(t534)) gm_trap_index(t533, t534, gm_src4tam33qo20j7, 254, 28);
                                uint8_t t535 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l50) + 24)) + t533 * 40) + 20));
                                uint8_t t536 = (uint8_t)!t535;
                                if (!t536) goto ie530;
                                {
                                    void * t539 = gm_l0;
                                    void * t540 = gm_l55;
                                    gm_t9805qtonbsag t541 = gm_f_stack_target_7vr78v6(t539, t540);
                                    gm_t9805qtonbsag t542 = t541;
                                    if (!((((uint8_t *)&t542))[4] != 0)) goto ie538;
                                    uint32_t t543 = GM_LD(uint32_t, ((uint8_t *)&t542));
                                    gm_l56 = t543;
                                    {
                                        void * t545 = gm_l0;
                                        uint64_t t546 = GM_LD(uintptr_t, ((((uint8_t *)t545) + 8) + 0) + 8);
                                        uint64_t r547; bool o548 = __builtin_sub_overflow(t546, ((uint64_t)1ULL), &r547);
                                        if (o548) gm_trap("integer overflow in `-`", gm_src4tam33qo20j7, 256, 51);
                                        uint64_t t549 = r547;
                                        uint64_t t550 = (uintptr_t)t549;
                                        void * t551 = gm_l0;
                                        uint64_t t552 = GM_LD(uintptr_t, ((((uint8_t *)t551) + 8) + 0) + 8);
                                        if (t550 >= (uintptr_t)(t552)) gm_trap_index(t550, t552, gm_src4tam33qo20j7, 256, 38);
                                        void * t553 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t551) + 8) + 0)) + t550 * 48) + 8);
                                        uint32_t t554 = gm_l54;
                                        uint32_t t555 = gm_l56;
                                        gm_b0_1 t556 = gm_f_bind_fn_pefln9r((void *)NULL, cx, NULL, t553, t554, t555);
                                        if (gm_unw.kind) {
                                            goto gm_ll516;
                                        }
                                    }
                                    goto ix538;
ie538:;
ix538:;
                                }
                                goto ix530;
ie530:;
ix530:;
                            }
                            goto ix525;
ie525:;
ix525:;
                        }
                        goto me521;
mn522:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 252, 13);
me521:;
                    }
lc512:;
                    {
                        uint64_t t558 = gm_l52;
                        uint64_t r559; bool o560 = __builtin_add_overflow(t558, ((uint64_t)1ULL), &r559);
                        uint64_t t561 = r559;
                        gm_l52 = t561;
                    }
                }
gm_ll516:;
                if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb512; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc512; }
                }
                goto gm_lad;
lb512:;
            }
        }
        {
            {
                void * t563 = gm_l1;
                gm_b16_8 t564 = gm_f_stack_children_dedraea((void *)NULL, cx, NULL, t563);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l57 = t564;
            }
            {
                gm_l58 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t566 = gm_l58;
                    uint64_t t567 = GM_LD(uintptr_t, ((uint8_t *)&gm_l57) + 8);
                    uint8_t t568 = (t566 < t567);
                    if (!t568) goto lb565;
                    {
                        uint64_t t571 = gm_l58;
                        uint64_t t572 = (uintptr_t)t571;
                        uint64_t t573 = GM_LD(uintptr_t, ((uint8_t *)&gm_l57) + 8);
                        void * t574 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l57)) + t572 * 8));
                        void * t575 = t574;
                        void * t578 = GM_LD(void *, ((uint8_t *)&t575));
                        gm_l59 = t578;
                        {
                            uint64_t t580 = gm_l2;
                            void * t581 = gm_l0;
                            void * t582 = gm_l59;
                            gm_toabfckqu3onu t583;
                            gm_f_stack_expr_33aekgt((void *)&t583, (void *)NULL, cx, NULL, t581, t582);
                            if (gm_unw.kind) {
                                goto gm_ll569;
                            }
                            gm_toabfckqu3onu t584 = t583;
                            if (((((uint8_t *)&t584))[0] != 0)) {
                                gm_tgeh6n0c7c8kb t585 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t584) + 8));
                                gm_toabfckqu3onu t586;
                                GM_ST(uint8_t, (uint8_t *)&t586 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t586 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t586 + 4, 0);
                                ((uint8_t *)&t586)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t586 + 8), t585);
                                F.ret = t586;
                                goto gm_fret;
                            }
                            uint64_t t587 = GM_LD(uint64_t, (((uint8_t *)&t584) + 8));
                            uint64_t t588 = (t580 > t587 ? t580 : t587);
                            gm_l2 = t588;
                        }
                        goto me576;
mn577:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 261, 13);
me576:;
                    }
lc565:;
                    {
                        uint64_t t590 = gm_l58;
                        uint64_t r591; bool o592 = __builtin_add_overflow(t590, ((uint64_t)1ULL), &r591);
                        uint64_t t593 = r591;
                        gm_l58 = t593;
                    }
                }
gm_ll569:;
                if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb565; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc565; }
                }
                goto gm_lad;
lb565:;
            }
        }
        goto me15;
mn485:;
ma20:;
        {
            {
                void * t597 = gm_l1;
                gm_b16_8 t598 = gm_f_stack_children_dedraea((void *)NULL, cx, NULL, t597);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l60 = t598;
            }
            {
                gm_l61 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t600 = gm_l61;
                    uint64_t t601 = GM_LD(uintptr_t, ((uint8_t *)&gm_l60) + 8);
                    uint8_t t602 = (t600 < t601);
                    if (!t602) goto lb599;
                    {
                        uint64_t t605 = gm_l61;
                        uint64_t t606 = (uintptr_t)t605;
                        uint64_t t607 = GM_LD(uintptr_t, ((uint8_t *)&gm_l60) + 8);
                        void * t608 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l60)) + t606 * 8));
                        void * t609 = t608;
                        void * t612 = GM_LD(void *, ((uint8_t *)&t609));
                        gm_l62 = t612;
                        {
                            uint64_t t614 = gm_l2;
                            void * t615 = gm_l0;
                            void * t616 = gm_l62;
                            gm_toabfckqu3onu t617;
                            gm_f_stack_expr_33aekgt((void *)&t617, (void *)NULL, cx, NULL, t615, t616);
                            if (gm_unw.kind) {
                                goto gm_ll603;
                            }
                            gm_toabfckqu3onu t618 = t617;
                            if (((((uint8_t *)&t618))[0] != 0)) {
                                gm_tgeh6n0c7c8kb t619 = GM_LD(gm_tgeh6n0c7c8kb, (((uint8_t *)&t618) + 8));
                                gm_toabfckqu3onu t620;
                                GM_ST(uint8_t, (uint8_t *)&t620 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t620 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t620 + 4, 0);
                                ((uint8_t *)&t620)[0] = 1;
                                GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t620 + 8), t619);
                                F.ret = t620;
                                goto gm_fret;
                            }
                            uint64_t t621 = GM_LD(uint64_t, (((uint8_t *)&t618) + 8));
                            uint64_t t622 = (t614 > t621 ? t614 : t621);
                            gm_l2 = t622;
                        }
                        goto me610;
mn611:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 264, 13);
me610:;
                    }
lc599:;
                    {
                        uint64_t t624 = gm_l61;
                        uint64_t r625; bool o626 = __builtin_add_overflow(t624, ((uint64_t)1ULL), &r625);
                        uint64_t t627 = r625;
                        gm_l61 = t627;
                    }
                }
gm_ll603:;
                if (gm_unw.frame == &F.h && gm_unw.label == 8) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb599; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc599; }
                }
                goto gm_lad;
lb599:;
            }
        }
        goto me15;
mn594:;
ma21:;
        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 178, 5);
me15:;
    }
    {
        uint64_t t628 = gm_l2;
        gm_toabfckqu3onu t629;
        memset(&t629, 0, sizeof t629);
        ((uint8_t *)&t629)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t629 + 8), t628);
        gm_toabfckqu3onu t630 = t629;
        uint8_t t631 = ((((uint8_t *)&t630))[0] != 0);
        F.ret = t629;
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
inline __attribute__((aligned(4))) uint64_t gm_f_stack_frame_68512ca(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_stack_frame_68512ca F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* g */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tb31k5l794t7p gm_l4; /* c */
    uint64_t gm_l6; /* c */
    GM_STACK_CHECK(&F, 348u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 128) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tb31k5l794t7p t16 = GM_LD(gm_tb31k5l794t7p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 16));
                    gm_l4 = t16;
                    {
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
                        uint32_t t20 = gm_l1;
                        uint8_t t21 = ((t19 == t20));
                        if (!t21) goto ie18;
                        {
                            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            uint8_t t24 = 0;
                            F.ret = t23;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 132, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t26 = gm_l3;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l3 = t29;
                }
            }
lb5:;
        }
    }
    {
        void * t30 = gm_l0;
        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
        uint32_t t32 = gm_l1;
        gm_f_def_of_m512sbu((void *)&F.l5, t31, t32);
    }
    {
        void * t33 = gm_l0;
        void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 24));
        gm_b16_8 t36 = t35;
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        void * t38 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
        uint64_t t39 = gm_f_frame_cost_oei64tv((void *)NULL, cx, NULL, t34, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t39;
    }
    {
        void * t40 = gm_l0;
        void * t41 = (void *)(((uint8_t *)t40) + 128);
        uint32_t t42 = gm_l1;
        uint64_t t43 = gm_l6;
        gm_tb31k5l794t7p t44;
        GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t44 + 0), t42);
        GM_ST(uint64_t, ((uint8_t *)&t44 + 8), t43);
        gm_tftb1odc739t5 t45 = gm_f_push_1mcf4ec(t41, GM_CONV(gm_cskjv6t053tkje, t44));
        gm_tftb1odc739t5 t46 = t45;
        if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_src4tam33qo20j7, 137, 5);
        gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
    }
    {
        uint64_t t48 = gm_l6;
        uint8_t t49 = 0;
        F.ret = t48;
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
__attribute__((aligned(4))) void gm_f_stack_perform_soivemm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_stack_perform_soivemm F;
    void * gm_l0; /* a */
    void * gm_l1; /* eref */
    uint32_t gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* ops */
    gm_b16_8 gm_l6; /* unknown */
    uint64_t gm_l7; /* k */
    GM_STACK_CHECK(&F, 1232u);
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
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 0));
        uint64_t t4 = (uint64_t)t3;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 1048) + 0) + 8);
        if (t5 >= (uintptr_t)(t8)) gm_trap_index(t5, t8, gm_src4tam33qo20j7, 289, 12);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 1048) + 0)) + t5 * 56) + 40));
        gm_l5 = t9;
    }
    {
        uint32_t t11 = gm_l2;
        uint64_t t12 = (uint64_t)t11;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        uint8_t t14 = (t12 < t13);
        if (!t14) goto ie10;
        uint32_t t15 = gm_l2;
        uint64_t t16 = (uint64_t)t15;
        uint64_t t17 = (uintptr_t)t16;
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_src4tam33qo20j7, 291, 32);
        uint8_t t19 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t17 * 48) + 16) + 0) == ((uint8_t)2ULL)));
        if (!t19) goto ie10;
        {
            gm_toabfckqu3onu t21;
            memset(&t21, 0, sizeof t21);
            ((uint8_t *)&t21)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t21 + 8), ((uint64_t)0ULL));
            gm_toabfckqu3onu t22 = t21;
            uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
            F.ret = t21;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = (void *)(((uint8_t *)t25) + 8);
        void * t27 = gm_l1;
        gm_b16_8 t28 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t29;
        GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t28);
        gm_tabb710d9mq2e t30;
        gm_f_format_845n386((void *)&t30, (void *)NULL, cx, NULL, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t31 = t30;
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_src4tam33qo20j7, 292, 16);
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
        gm_l6 = t32;
    }
    {
        void * t33 = gm_l0;
        uint64_t t34 = GM_LD(uintptr_t, ((((uint8_t *)t33) + 48) + 0) + 8);
        gm_l7 = t34;
    }
    {
        for (;;) {
            uint64_t t36 = gm_l7;
            uint8_t t37 = (t36 > ((uint64_t)0ULL));
            if (!t37) goto lb35;
            {
                uint64_t t40 = gm_l7;
                uint64_t r41; bool o42 = __builtin_sub_overflow(t40, ((uint64_t)1ULL), &r41);
                if (o42) gm_trap("integer overflow in `-`", gm_src4tam33qo20j7, 295, 9);
                uint64_t t43 = r41;
                gm_l7 = t43;
            }
            {
                uint64_t t45 = gm_l7;
                uint64_t t46 = (uintptr_t)t45;
                void * t47 = gm_l0;
                uint64_t t48 = GM_LD(uintptr_t, ((((uint8_t *)t47) + 48) + 0) + 8);
                if (t46 >= (uintptr_t)(t48)) gm_trap_index(t46, t48, gm_src4tam33qo20j7, 296, 27);
                void * t49 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t47) + 48) + 0)) + t46 * 40) + 0);
                void * t50 = gm_l1;
                int32_t t51 = gm_f_cmp_ref_8ntrqll(t49, t50);
                uint8_t t52 = (!(t51 == ((int32_t)0LL)));
                if (!t52) goto ie44;
                {
                    goto lc35;
                }
                goto ix44;
ie44:;
ix44:;
            }
            {
                uint64_t t54 = gm_l7;
                uint64_t t55 = (uintptr_t)t54;
                void * t56 = gm_l0;
                uint64_t t57 = GM_LD(uintptr_t, ((((uint8_t *)t56) + 48) + 0) + 8);
                if (t55 >= (uintptr_t)(t57)) gm_trap_index(t55, t57, gm_src4tam33qo20j7, 297, 14);
                gm_t1cs2fjnrqap1 t58 = GM_LD(gm_t1cs2fjnrqap1, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t56) + 48) + 0)) + t55 * 40));
                F.l8 = t58;
            }
            {
                uint32_t t60 = gm_l2;
                uint64_t t61 = (uint64_t)t60;
                uint64_t t62 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 24) + 8);
                uint8_t t63 = (t61 >= t62);
                if (!t63) goto ie59;
                {
                    gm_tbgd14esf0jvi t65;
                    memset((uint8_t *)&t65 + 12, 0, 4);
                    gm_tl874t2atnlh3 t66 = gm_l4;
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t65 + 0, t66);
                    gm_b16_8 t67 = gm_l6;
                    gm_b16_8 t68 = t67;
                    gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                    GM_ST(gm_b16_8, (uint8_t *)&t65 + 16, t69);
                    gm_tgeh6n0c7c8kb t70;
                    GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t70 + 8), t65);
                    gm_toabfckqu3onu t71;
                    GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
                    ((uint8_t *)&t71)[0] = 1;
                    GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t71 + 8), t70);
                    gm_toabfckqu3onu t72 = t71;
                    uint8_t t73 = ((((uint8_t *)&t72))[0] != 0);
                    F.ret = t71;
                    goto gm_fret;
                }
                goto ix59;
ie59:;
ix59:;
            }
            {
                void * t74 = gm_l0;
                uint32_t t75 = gm_l2;
                uint64_t t76 = (uint64_t)t75;
                uint64_t t77 = (uintptr_t)t76;
                uint64_t t78 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 24) + 8);
                if (t77 >= (uintptr_t)(t78)) gm_trap_index(t77, t78, gm_src4tam33qo20j7, 299, 30);
                uint32_t t79 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 24)) + t77 * 4));
                gm_b16_8 t80 = gm_l3;
                void * t81 = gm_l0;
                uint64_t t82 = GM_LD(uintptr_t, ((((uint8_t *)t81) + 48) + 0) + 8);
                void * t83 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t81) + 48) + 0)));
                uint64_t t84 = gm_l7;
                uint64_t t85 = (uintptr_t)t84;
                if ((uintptr_t)0 > t85 || t85 > (uintptr_t)t82) gm_trap_slice((uintptr_t)0, t85, t82, gm_src4tam33qo20j7, 299, 58);
                gm_b16_8 t86;
                GM_ST(void *, (uint8_t *)&t86, (void *)(((uint8_t *)t83 + (uintptr_t)0 * 40)));
                GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(t85 - (uintptr_t)0));
                gm_b16_8 t87 = t86;
                gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
                gm_tl874t2atnlh3 t89 = gm_l4;
                gm_f_stack_call_poqmlno((void *)&(*(gm_toabfckqu3onu *)gm_out), (void *)NULL, cx, NULL, t74, t79, t80, t88, t89);
                if (gm_unw.kind) {
                    goto gm_ll38;
                }
                goto gm_fdone;
            }
lc35:;
        }
gm_ll38:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb35; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc35; }
        }
        goto gm_lad;
lb35:;
    }
    {
        gm_tbgd14esf0jvi t90;
        memset((uint8_t *)&t90 + 12, 0, 4);
        gm_tl874t2atnlh3 t91 = gm_l4;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t90 + 0, t91);
        gm_b16_8 t92 = gm_l6;
        gm_b16_8 t93 = t92;
        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
        GM_ST(gm_b16_8, (uint8_t *)&t90 + 16, t94);
        gm_tgeh6n0c7c8kb t95;
        GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t95 + 8), t90);
        gm_toabfckqu3onu t96;
        GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t96 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t96 + 4, 0);
        ((uint8_t *)&t96)[0] = 1;
        GM_ST(gm_tgeh6n0c7c8kb, ((uint8_t *)&t96 + 8), t95);
        gm_toabfckqu3onu t97 = t96;
        uint8_t t98 = ((((uint8_t *)&t97))[0] != 0);
        F.ret = t96;
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
inline __attribute__((aligned(4))) void gm_f_stack_push_expr_bdl56rb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, void * a3) {
    struct gm_fr_stack_push_expr_bdl56rb F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* g */
    void * gm_l3; /* body */
    GM_STACK_CHECK(&F, 181u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_tha5675rv512q t4;
        memset((uint8_t *)&t4 + 4, 0, 4);
        uint32_t t5 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t4 + 0, t5);
        memcpy((uint8_t *)&t4 + 8, ((uint8_t *)&F.l2), 40);
        gm_tftb1odc739t5 t6 = gm_f_push_9gqhmau(t3, &t4);
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_src4tam33qo20j7, 124, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l3;
        gm_f_stack_expr_33aekgt((void *)&F.l4, (void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t12;
        if (!((((uint8_t *)&F.l4))[0] != 0)) goto mn14;
        t12 = ((uint8_t)0);
        goto me13;
mn14:;
        t12 = ((uint8_t)1);
        goto me13;
mn15:;
        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 126, 8);
me13:;
        if (!t12) goto ie11;
        {
            {
                void * t18 = gm_l0;
                void * t19 = (void *)(((uint8_t *)t18) + 8);
                gm_tijr008e33o2m t20;
                gm_f_pop_d7svgs5((void *)&t20, t19);
            }
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l4), 48);
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_stack_size_0gijrqq(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_stack_size_0gijrqq F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_tl874t2atnlh3 gm_l5; /* at */
    gm_b16_8 gm_l6; /* why */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l11; /* name */
    uint64_t gm_l13; /* frames */
    GM_STACK_CHECK(&F, 1227u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        GM_ST(void *, (uint8_t *)&F.l3 + 0, t2);
        gm_f_list_pe70gj7((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 8)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_o00nc4s((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 48)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_msjtuv4((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 88)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_msjtuv4((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 128)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l3);
        uint32_t t4 = gm_l1;
        gm_b16_8 t5 = gm_f_empty_slice_g3tddrb_2();
        gm_b16_8 t6 = t5;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_b16_8 t8 = gm_f_empty_slice_vndn3a4();
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tl874t2atnlh3 t11 = gm_l2;
        gm_f_stack_call_poqmlno((void *)&F.l4, (void *)NULL, cx, NULL, t3, t4, t7, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((uint8_t *)&F.l4))[0] != 0)) goto mn13;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        if (t14 != ((uint8_t)0ULL)) goto mn13;
        gm_tf5srh4mp4b3i t15;
        memset(&t15, 0, sizeof t15);
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
        goto me12;
mn13:;
        if (!((((uint8_t *)&F.l4))[0] != 0)) goto mn17;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        if (t18 != ((uint8_t)1ULL)) goto mn17;
        gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)&F.l4) + 8) + 8) + 0));
        gm_l5 = t19;
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l4) + 8) + 8) + 16));
        gm_l6 = t20;
        {
            gm_f_list_epvducg((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t23 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 0));
                gm_l8 = t23;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t25 = gm_l9;
                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t27 = (t25 < t26);
                    if (!t27) goto lb24;
                    {
                        uint64_t t30 = gm_l9;
                        uint64_t t31 = (uintptr_t)t30;
                        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        gm_tha5675rv512q t35 = GM_LD(gm_tha5675rv512q, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t31 * 48));
                        F.l10 = t35;
                        {
                            void * t37 = (void *)((uint8_t *)&F.l7);
                            uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)&F.l10) + 0));
                            uint64_t t39 = (uint64_t)t38;
                            uint64_t t40 = (uintptr_t)t39;
                            void * t41 = gm_l0;
                            uint64_t t42 = GM_LD(uintptr_t, ((((uint8_t *)t41) + 344) + 0) + 8);
                            if (t40 >= (uintptr_t)(t42)) gm_trap_index(t40, t42, gm_src4tam33qo20j7, 71, 68);
                            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t41) + 344) + 0)) + t40 * 136) + 0));
                            gm_tnpifv20irnph t44;
                            GM_ST(gm_b16_8, ((uint8_t *)&t44 + 0), t43);
                            gm_tabb710d9mq2e t45;
                            gm_f_format_h4jcodi((void *)&t45, (void *)NULL, cx, NULL, t44);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            gm_tabb710d9mq2e t46 = t45;
                            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_src4tam33qo20j7, 71, 49);
                            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
                            gm_b16_8 t48 = t47;
                            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                            gm_tftb1odc739t5 t50 = gm_f_push_3ef746b(t37, t49);
                            gm_tftb1odc739t5 t51 = t50;
                            if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_src4tam33qo20j7, 71, 38);
                            gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
                        }
                        goto me33;
mn34:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 71, 13);
me33:;
                    }
lc24:;
                    {
                        uint64_t t54 = gm_l9;
                        uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                        uint64_t t57 = r55;
                        gm_l9 = t57;
                    }
                }
gm_ll28:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb24; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc24; }
                }
                goto gm_lad;
lb24:;
            }
        }
        {
            uint32_t t58 = gm_l1;
            uint64_t t59 = (uint64_t)t58;
            uint64_t t60 = (uintptr_t)t59;
            void * t61 = gm_l0;
            uint64_t t62 = GM_LD(uintptr_t, ((((uint8_t *)t61) + 344) + 0) + 8);
            if (t60 >= (uintptr_t)(t62)) gm_trap_index(t60, t62, gm_src4tam33qo20j7, 72, 21);
            gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t61) + 344) + 0)) + t60 * 136) + 0));
            gm_l11 = t63;
        }
        {
            gm_tl874t2atnlh3 t64 = gm_l5;
            gm_t6j6smj7qoctu t65;
            gm_b16_8 t66 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t65 + 0, t66);
            gm_b16_8 t67 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t65 + 16, t67);
            gm_tabb710d9mq2e t68;
            gm_f_format_t68frbc((void *)&t68, (void *)NULL, cx, NULL, &t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t69 = t68;
            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_src4tam33qo20j7, 73, 33);
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
            gm_b16_8 t71 = t70;
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
            gm_f_error_s6hn871((void *)&F.l12, (void *)NULL, cx, NULL, t64, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t73 = (void *)((uint8_t *)&F.l12);
            gm_tl874t2atnlh3 t74 = gm_l2;
            gm_b16_8 t75;
            GM_ST(void *, (uint8_t *)&t75, (void *)(gm_sdscrog2npict));
            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(22));
            gm_f_add_label_50fj7vv(t73, GM_CONV(gm_cseahvg8v7p0vs, t74), t75);
        }
        {
            uint64_t t77 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
            uint8_t t78 = (t77 > ((uint64_t)1ULL));
            if (!t78) goto ie76;
            {
                void * t80 = (void *)((uint8_t *)&F.l12);
                gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                gm_b16_8 t82 = t81;
                gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
                gm_b16_8 t84;
                GM_ST(void *, (uint8_t *)&t84, (void *)(gm_sdsrtcf8ng48d));
                GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(5));
                gm_tabb710d9mq2e t85;
                gm_f_join_sfknljb((void *)&t85, (void *)NULL, cx, NULL, t83, t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t86 = t85;
                if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 8, gm_src4tam33qo20j7, 75, 82);
                gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
                gm_tnpifv20irnph t88;
                GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t87);
                gm_tabb710d9mq2e t89;
                gm_f_format_hp1rthv((void *)&t89, (void *)NULL, cx, NULL, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t90 = t89;
                if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 8, gm_src4tam33qo20j7, 75, 49);
                gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&t90) + 8));
                gm_b16_8 t92 = t91;
                gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                gm_f_add_note_46v502s(t80, t93);
            }
            goto ix76;
ie76:;
ix76:;
        }
        {
            void * t94 = gm_l0;
            gm_b0_1 t95 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t94, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l12)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tf5srh4mp4b3i t96;
            memset(&t96, 0, sizeof t96);
            uint8_t t97 = 0;
            F.ret = t96;
            goto gm_fret;
        }
        goto me12;
mn17:;
        if (((((uint8_t *)&F.l4))[0] != 0)) goto mn98;
        uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
        gm_l13 = t99;
        uint64_t t100 = GM_LD(uint64_t, gm_cb7r52n9k7f);
        uint64_t t101 = GM_LD(uint64_t, gm_cilec57f4ei);
        uint64_t r102; bool o103 = __builtin_add_overflow(t100, t101, &r102);
        if (o103) gm_trap("integer overflow in `+`", gm_src4tam33qo20j7, 79, 28);
        uint64_t t104 = r102;
        uint64_t t105 = gm_l13;
        uint64_t r106; bool o107 = __builtin_add_overflow(t104, t105, &r106);
        uint64_t t108 = o107 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r106;
        uint64_t t109 = GM_LD(uint64_t, gm_cdlh2b67kra);
        uint64_t t110 = (t108 > t109 ? t108 : t109);
        gm_tf5srh4mp4b3i t111;
        GM_ST(uint8_t, (uint8_t *)&t111 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t111 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t111 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t111), t110);
        ((uint8_t *)&t111)[8] = 1;
        uint8_t t112 = 0;
        F.ret = t111;
        goto gm_fret;
        goto me12;
mn98:;
        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 67, 5);
me12:;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_stack_target_7vr78v6(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stack_target_7vr78v6 F;
    void * gm_l0; /* a */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* f */
    uint32_t gm_l3; /* f */
    void * gm_l4; /* x */
    uint32_t gm_l5; /* l */
    uint32_t gm_l7; /* levels */
    uint32_t gm_l8; /* local */
    uint32_t gm_l10; /* owner */
    uint32_t gm_l11; /* <tmp> */
    uint32_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    GM_STACK_CHECK(&F, 1430u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma4; case ((uint8_t)27ULL): goto ma4; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma5; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)26ULL)) goto pa13;
        uint32_t t15 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t15;
        goto po12;
pa13:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)27ULL)) goto pa16;
        uint32_t t18 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t18;
        goto po12;
pa16:;
        goto mn11;
po12:;
        uint32_t t19 = gm_l2;
        gm_t9805qtonbsag t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t20), t19);
        ((uint8_t *)&t20)[4] = 1;
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
        goto me3;
mn11:;
ma5:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t23 != ((uint8_t)32ULL)) goto mn22;
        void * t24 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l4 = t24;
        void * t25 = gm_l0;
        void * t26 = gm_l4;
        gm_t9805qtonbsag t27 = gm_f_stack_target_7vr78v6(t25, t26);
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
        goto me3;
mn22:;
ma6:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t30 != ((uint8_t)15ULL)) goto mn29;
        uint32_t t31 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t31;
        {
            void * t33 = gm_l0;
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((((uint8_t *)t33) + 8) + 0));
            gm_b16_8 t35 = t34;
            void * t36 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t35)));
            uint64_t t37 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t35) + 8);
            uint64_t t38 = (t37 - 1);
            gm_tijr008e33o2m t39;
            if (t38 < t37) {
            gm_tha5675rv512q t40 = GM_LD(gm_tha5675rv512q, ((uint8_t *)t36 + t38 * 48));
            gm_tijr008e33o2m t41;
            GM_ST(uint8_t, (uint8_t *)&t41 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t41 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t41 + 52, 0);
            GM_ST(gm_tha5675rv512q, ((uint8_t *)&t41), t40);
            ((uint8_t *)&t41)[48] = 1;
                t39 = t41;
            } else {
            gm_tijr008e33o2m t42;
            memset(&t42, 0, sizeof t42);
                t39 = t42;
            }
            gm_tijr008e33o2m t43 = t39;
            gm_tha5675rv512q t44;
            if (((((uint8_t *)&t43))[48] != 0)) {
                gm_tha5675rv512q t45 = GM_LD(gm_tha5675rv512q, (((uint8_t *)&t43) + 0));
                t44 = t45;
            } else {
                gm_t9805qtonbsag t46;
                memset(&t46, 0, sizeof t46);
                uint8_t t47 = 0;
                F.ret = t46;
                goto gm_fret;
            }
            F.l6 = t44;
        }
        {
            void * t48 = (void *)(((uint8_t *)&F.l6) + 8);
            uint32_t t49 = gm_l5;
            gm_t9805qtonbsag t50 = gm_f_bound_of_sn02ojm(t48, t49);
            uint8_t t51 = 0;
            F.ret = t50;
            goto gm_fret;
        }
        goto me3;
mn29:;
ma7:;
        uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t53 != ((uint8_t)16ULL)) goto mn52;
        uint32_t t54 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l7 = t54;
        uint32_t t55 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 4));
        gm_l8 = t55;
        {
            void * t57 = gm_l0;
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((((uint8_t *)t57) + 8) + 0));
            gm_b16_8 t59 = t58;
            void * t60 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t59)));
            uint64_t t61 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t59) + 8);
            uint64_t t62 = (t61 - 1);
            gm_tijr008e33o2m t63;
            if (t62 < t61) {
            gm_tha5675rv512q t64 = GM_LD(gm_tha5675rv512q, ((uint8_t *)t60 + t62 * 48));
            gm_tijr008e33o2m t65;
            GM_ST(uint8_t, (uint8_t *)&t65 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t65 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t65 + 52, 0);
            GM_ST(gm_tha5675rv512q, ((uint8_t *)&t65), t64);
            ((uint8_t *)&t65)[48] = 1;
                t63 = t65;
            } else {
            gm_tijr008e33o2m t66;
            memset(&t66, 0, sizeof t66);
                t63 = t66;
            }
            gm_tijr008e33o2m t67 = t63;
            gm_tha5675rv512q t68;
            if (((((uint8_t *)&t67))[48] != 0)) {
                gm_tha5675rv512q t69 = GM_LD(gm_tha5675rv512q, (((uint8_t *)&t67) + 0));
                t68 = t69;
            } else {
                gm_t9805qtonbsag t70;
                memset(&t70, 0, sizeof t70);
                uint8_t t71 = 0;
                F.ret = t70;
                goto gm_fret;
            }
            F.l9 = t68;
        }
        {
            uint32_t t72 = GM_LD(uint32_t, (((uint8_t *)&F.l9) + 0));
            gm_l10 = t72;
        }
        {
            {
                gm_l12 = ((uint32_t)0ULL);
            }
            {
                uint32_t t74 = gm_l7;
                gm_l11 = t74;
            }
            {
                for (;;) {
                    uint32_t t76 = gm_l12;
                    uint32_t t77 = gm_l11;
                    uint8_t t78 = (t76 < t77);
                    if (!t78) goto lb75;
                    {
                        uint32_t t81 = gm_l12;
                        uint32_t t82 = t81;
                        {
                            void * t86 = gm_l0;
                            void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
                            uint32_t t88 = gm_l10;
                            gm_tdgfduqjheqdm t89;
                            gm_f_def_of_m512sbu((void *)&t89, t87, t88);
                            gm_tdgfduqjheqdm t90 = t89;
                            gm_t9805qtonbsag t91 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&t90) + 48));
                            gm_t9805qtonbsag t92 = t91;
                            uint32_t t93;
                            if (((((uint8_t *)&t92))[4] != 0)) {
                                uint32_t t94 = GM_LD(uint32_t, (((uint8_t *)&t92) + 0));
                                t93 = t94;
                            } else {
                                gm_t9805qtonbsag t95;
                                memset(&t95, 0, sizeof t95);
                                uint8_t t96 = 0;
                                F.ret = t95;
                                goto gm_fret;
                            }
                            gm_l10 = t93;
                        }
                        goto me83;
mn84:;
                        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 153, 13);
me83:;
                    }
lc75:;
                    {
                        uint32_t t98 = gm_l12;
                        uint32_t r99; bool o100 = __builtin_add_overflow(t98, ((uint32_t)1ULL), &r99);
                        uint32_t t101 = r99;
                        gm_l12 = t101;
                    }
                }
lb75:;
            }
        }
        {
            void * t102 = gm_l0;
            uint64_t t103 = GM_LD(uintptr_t, ((((uint8_t *)t102) + 8) + 0) + 8);
            gm_l13 = t103;
        }
        {
            for (;;) {
                uint64_t t105 = gm_l13;
                uint8_t t106 = (t105 > ((uint64_t)0ULL));
                if (!t106) goto lb104;
                {
                    uint64_t t109 = gm_l13;
                    uint64_t r110; bool o111 = __builtin_sub_overflow(t109, ((uint64_t)1ULL), &r110);
                    if (o111) gm_trap("integer overflow in `-`", gm_src4tam33qo20j7, 158, 17);
                    uint64_t t112 = r110;
                    gm_l13 = t112;
                }
                {
                    uint64_t t114 = gm_l13;
                    uint64_t t115 = (uintptr_t)t114;
                    void * t116 = gm_l0;
                    uint64_t t117 = GM_LD(uintptr_t, ((((uint8_t *)t116) + 8) + 0) + 8);
                    if (t115 >= (uintptr_t)(t117)) gm_trap_index(t115, t117, gm_src4tam33qo20j7, 159, 20);
                    uint32_t t118 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t116) + 8) + 0)) + t115 * 48) + 0));
                    uint32_t t119 = gm_l10;
                    uint8_t t120 = ((t118 == t119));
                    if (!t120) goto ie113;
                    {
                        uint64_t t122 = gm_l13;
                        uint64_t t123 = (uintptr_t)t122;
                        void * t124 = gm_l0;
                        uint64_t t125 = GM_LD(uintptr_t, ((((uint8_t *)t124) + 8) + 0) + 8);
                        if (t123 >= (uintptr_t)(t125)) gm_trap_index(t123, t125, gm_src4tam33qo20j7, 159, 66);
                        void * t126 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t124) + 8) + 0)) + t123 * 48) + 8);
                        uint32_t t127 = gm_l8;
                        gm_t9805qtonbsag t128 = gm_f_bound_of_sn02ojm(t126, t127);
                        uint8_t t129 = 0;
                        F.ret = t128;
                        goto gm_fret;
                    }
                    goto ix113;
ie113:;
ix113:;
                }
lc104:;
            }
lb104:;
        }
        {
            gm_t9805qtonbsag t130;
            memset(&t130, 0, sizeof t130);
            uint8_t t131 = 0;
            F.ret = t130;
            goto gm_fret;
        }
        goto me3;
mn52:;
ma8:;
        gm_t9805qtonbsag t133;
        memset(&t133, 0, sizeof t133);
        uint8_t t134 = 0;
        F.ret = t133;
        goto gm_fret;
        goto me3;
mn132:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_src4tam33qo20j7, 143, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
