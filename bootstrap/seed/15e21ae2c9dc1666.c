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
typedef struct gm_cs6mnftt383dnt { void * f0; void * f1; } gm_cs6mnftt383dnt;
typedef struct gm_cskjv6t053tkje { uint32_t f0; uint64_t f1; } gm_cskjv6t053tkje;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; } gm_t2abnvcnt53ot;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; } f1; } gm_t8c5h7u09n10a;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t f6; uint8_t f7; uint8_t f8; uint8_t f9; uint8_t f10; uint8_t f11; uint8_t f12; uint8_t f13; uint8_t f14; uint8_t f15; uint8_t f16; uint8_t f17; } gm_ta72bg8o6meed;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_cskjv6t053tkje) == 16 && _Alignof(gm_cskjv6t053tkje) == 8 && offsetof(gm_cskjv6t053tkje, f0) == 0 && offsetof(gm_cskjv6t053tkje, f1) == 8, "layout of `Char`");
_Static_assert(sizeof(gm_t2abnvcnt53ot) == 4 && _Alignof(gm_t2abnvcnt53ot) == 1 && offsetof(gm_t2abnvcnt53ot, f0) == 0 && offsetof(gm_t2abnvcnt53ot, f1) == 1 && offsetof(gm_t2abnvcnt53ot, f2) == 2 && offsetof(gm_t2abnvcnt53ot, f3) == 3, "layout of `(u8, u8, u8, u8)`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t f6; uint8_t f7; uint8_t pe[3]; } gm_t88jm1dgsmkjs;
_Static_assert(sizeof(gm_t8c5h7u09n10a) == 16 && _Alignof(gm_t8c5h7u09n10a) == 8 && offsetof(gm_t8c5h7u09n10a, f0) == 0 && offsetof(gm_t8c5h7u09n10a, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta72bg8o6meed) == 18 && _Alignof(gm_ta72bg8o6meed) == 1 && offsetof(gm_ta72bg8o6meed, f0) == 0 && offsetof(gm_ta72bg8o6meed, f1) == 1 && offsetof(gm_ta72bg8o6meed, f2) == 2 && offsetof(gm_ta72bg8o6meed, f3) == 3 && offsetof(gm_ta72bg8o6meed, f4) == 4 && offsetof(gm_ta72bg8o6meed, f5) == 5 && offsetof(gm_ta72bg8o6meed, f6) == 6 && offsetof(gm_ta72bg8o6meed, f7) == 7 && offsetof(gm_ta72bg8o6meed, f8) == 8 && offsetof(gm_ta72bg8o6meed, f9) == 9 && offsetof(gm_ta72bg8o6meed, f10) == 10 && offsetof(gm_ta72bg8o6meed, f11) == 11 && offsetof(gm_ta72bg8o6meed, f12) == 12 && offsetof(gm_ta72bg8o6meed, f13) == 13 && offsetof(gm_ta72bg8o6meed, f14) == 14 && offsetof(gm_ta72bg8o6meed, f15) == 15 && offsetof(gm_ta72bg8o6meed, f16) == 16 && offsetof(gm_ta72bg8o6meed, f17) == 17, "layout of `(u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8, u8)`");
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t88jm1dgsmkjs) == 40 && _Alignof(gm_t88jm1dgsmkjs) == 8 && offsetof(gm_t88jm1dgsmkjs, f0) == 0 && offsetof(gm_t88jm1dgsmkjs, f1) == 8 && offsetof(gm_t88jm1dgsmkjs, f2) == 16 && offsetof(gm_t88jm1dgsmkjs, f3) == 32 && offsetof(gm_t88jm1dgsmkjs, f4) == 33 && offsetof(gm_t88jm1dgsmkjs, f5) == 34 && offsetof(gm_t88jm1dgsmkjs, f6) == 35 && offsetof(gm_t88jm1dgsmkjs, f7) == 36, "layout of `Info`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t6j6smj7qoctu v1; gm_b16_8 v2; } f1; } gm_tj63kbqqj2092;
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint64_t f2; void * f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_tc0nbpdhofi37 f7; gm_trqfvmjtt4sij f8; gm_toceffb697p0u f9; gm_trqfvmjtt4sij f10; uint8_t f11; uint8_t pe[7]; } gm_trd6bg8is1v4n;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint64_t f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; gm_tc70a9dbbmgkg f3; uint8_t f4; uint8_t pe[7]; } gm_tsnno3vacsa4v;
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; } gm_t23tohc57ebhi;
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
_Static_assert(sizeof(gm_tj63kbqqj2092) == 40 && _Alignof(gm_tj63kbqqj2092) == 8 && offsetof(gm_tj63kbqqj2092, f0) == 0 && offsetof(gm_tj63kbqqj2092, f1) == 8, "layout of `Item`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_trd6bg8is1v4n) == 288 && _Alignof(gm_trd6bg8is1v4n) == 8 && offsetof(gm_trd6bg8is1v4n, f0) == 0 && offsetof(gm_trd6bg8is1v4n, f1) == 8 && offsetof(gm_trd6bg8is1v4n, f2) == 24 && offsetof(gm_trd6bg8is1v4n, f3) == 32 && offsetof(gm_trd6bg8is1v4n, f4) == 40 && offsetof(gm_trd6bg8is1v4n, f5) == 80 && offsetof(gm_trd6bg8is1v4n, f6) == 120 && offsetof(gm_trd6bg8is1v4n, f7) == 160 && offsetof(gm_trd6bg8is1v4n, f8) == 184 && offsetof(gm_trd6bg8is1v4n, f9) == 224 && offsetof(gm_trd6bg8is1v4n, f10) == 240 && offsetof(gm_trd6bg8is1v4n, f11) == 280, "layout of `Lexer`");
_Static_assert(sizeof(gm_tsnno3vacsa4v) == 48 && _Alignof(gm_tsnno3vacsa4v) == 8 && offsetof(gm_tsnno3vacsa4v, f0) == 0 && offsetof(gm_tsnno3vacsa4v, f1) == 8 && offsetof(gm_tsnno3vacsa4v, f2) == 9 && offsetof(gm_tsnno3vacsa4v, f3) == 16 && offsetof(gm_tsnno3vacsa4v, f4) == 40, "layout of `Frame`");
typedef struct { gm_tsnno3vacsa4v f0; uint8_t f1; uint8_t pe[7]; } gm_ttlblounidicj;
_Static_assert(sizeof(gm_t23tohc57ebhi) == 64 && _Alignof(gm_t23tohc57ebhi) == 8 && offsetof(gm_t23tohc57ebhi, f0) == 0 && offsetof(gm_t23tohc57ebhi, f1) == 32, "layout of `(TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
_Static_assert(sizeof(gm_ttlblounidicj) == 56 && _Alignof(gm_ttlblounidicj) == 8 && offsetof(gm_ttlblounidicj, f0) == 0 && offsetof(gm_ttlblounidicj, f1) == 48, "layout of `?Frame`");
struct gm_fr_aligned_with_s024agj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_allocator_5m783ja {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_column_r91o38o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_continues_line_2cliit4 {
    gm_frame h;
    gm_t88jm1dgsmkjs l0; /* info */
    uint8_t ret;
};
struct gm_fr_continues_with_name_ejtvq2k {
    gm_frame h;
    gm_t88jm1dgsmkjs l1; /* info */
    uint8_t ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpi80k4_4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p5lo8ed_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_punct_char_3bc3hom {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_word_kb7cdmu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_keeps_padding_m0v49h1 {
    gm_frame h;
    gm_t88jm1dgsmkjs l1; /* prev */
    gm_t88jm1dgsmkjs l2; /* cur */
    uint8_t ret;
};
struct gm_fr_layout_ufvu3cq {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* infos */
    gm_trqfvmjtt4sij l5; /* items */
    gm_t88jm1dgsmkjs l10; /* info */
    gm_trqfvmjtt4sij l11; /* out */
    gm_trqfvmjtt4sij l15; /* stack */
    gm_tj63kbqqj2092 l25; /* item */
    gm_ttlblounidicj l33; /* f */
    gm_tsnno3vacsa4v l35; /* fr */
    gm_tsnno3vacsa4v l43; /* f */
    gm_tsnno3vacsa4v l48; /* f */
    gm_t88jm1dgsmkjs l55; /* info */
    gm_t88jm1dgsmkjs l59; /* pinfo */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lex_dc8mbmf {
    gm_frame h;
    gm_trd6bg8is1v4n l3; /* lx */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_list_f0gjjbh_4 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g3vmkan_3 {
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
struct gm_fr_merges_with_tail_ijmh3nm {
    gm_frame h;
    gm_tjt4ka19str13 l4; /* scratch */
    gm_trqfvmjtt4sij l5; /* joined */
    gm_t2792tsg5eg72 l6; /* diags */
    gm_tr8dvlu9ik4gj l7; /* lexed */
    gm_tm4pc0emhrv1g l10; /* t */
    uint8_t ret;
    gm_b0_1 w1;
};
struct gm_fr_new_jr5ejs0 {
    gm_frame h;
    gm_tjt4ka19str13 ret;
};
struct gm_fr_next_tok_qap88op {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_out_column_hr4hgng {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_pop_f3uajlg_3 {
    gm_frame h;
    gm_tsnno3vacsa4v l2; /* x */
    gm_ttlblounidicj ret;
};
struct gm_fr_relative_column_ctuu0pd {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_scan_gap_tkq0f9l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_non_white_o0cd8c4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_would_merge_44g1m96 {
    gm_frame h;
    gm_tjt4ka19str13 l4; /* scratch */
    gm_trqfvmjtt4sij l5; /* joined */
    gm_t2792tsg5eg72 l6; /* diags */
    gm_tr8dvlu9ik4gj l7; /* lexed */
    gm_tm4pc0emhrv1g l13; /* t */
    uint8_t ret;
    gm_b0_1 w0;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b);
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b);
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_aligned_with_s024agj(gm_b16_8 a0, uint64_t a1, uint8_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_all_white_1b9moim(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0);
__attribute__((aligned(4))) uint64_t gm_f_char_count_76unqru(gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_char_offset_kr8000h(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_classify_e96658j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint64_t gm_f_column_r91o38o(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_continues_line_2cliit4(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_continues_line_2cliit4_c(gm_t88jm1dgsmkjs a0);
__attribute__((aligned(4))) uint8_t gm_f_continues_with_name_ejtvq2k(gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_continues_with_name_ejtvq2k_c(gm_b16_8 a0, gm_t88jm1dgsmkjs a1);
__attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_decode_jrk6b03(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_deinit_26uqnl1(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_aii0n0l(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_ectdopm_2(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_3(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
__attribute__((aligned(4))) uint64_t gm_f_indent_of_i911gpd(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_punct_char_3bc3hom(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_white_3njipsh(uint32_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_word_kb7cdmu(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_keeps_padding_m0v49h1(gm_b16_8 a0, const void * a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_keeps_padding_m0v49h1_c(gm_b16_8 a0, gm_t88jm1dgsmkjs a1, gm_t88jm1dgsmkjs a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_last_line_hk6q5l4(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_layout_ufvu3cq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_line_binds_jm0pnul(gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_line_end_jm825k5(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint64_t gm_f_line_start_pl10jt5(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g3vmkan_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_merges_with_tail_ijmh3nm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_tok_qap88op(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_only_spaces_bvta5sq(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_out_column_hr4hgng(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_f3uajlg_3(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1e500nc_4(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8h1r7s6_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_spaces_732bua4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_relative_column_ctuu0pd(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_replace_lf6hcmn(void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_gap_tkq0f9l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_non_white_o0cd8c4(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_d3d35bq(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_would_merge_44g1m96(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
extern uint8_t gm_c3dq62rrli9[16] __attribute__((aligned(8)));
extern uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
extern void *const gm_hd3t1u951rul[];
extern const uint8_t gm_s1c4sfff8ac39[7];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s920dpvvv6cej[4];
extern const uint8_t gm_s9es2tos1bhfp[5];
extern const uint8_t gm_sc8o4oaph9gbr[2];
extern const uint8_t gm_sccjki3qgi8m7[15];
extern const uint8_t gm_seccpktgmrevt[2];
extern const uint8_t gm_sf9a2kg3eo1gs[2];
extern const uint8_t gm_sfi2ha42r1nr2[2];
extern const uint8_t gm_sfqk3rro4g7t0[2];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_sh40na9qbga3l[2];
extern const uint8_t gm_sidtn67sdk4e0[7];
extern const uint8_t gm_sjfg2291kjugh[4];
extern const uint8_t gm_skmt2kt2p7v0u[3];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl9nvct67or45[3];
extern const uint8_t gm_slpo22sjs83b1[5];
extern const uint8_t gm_sm8obe455p2d9[3];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_soqt3jnmovq5l[4];
extern const uint8_t gm_spi47lg4tr80j[3];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srceoep1gquqdsq[];
extern const char gm_srcip69en21ht7v[];
extern const char gm_srcrchqfd7obmqe[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srh29phg8b30r[4];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_ss67f0gmfen1m[3];
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
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b) {
    if (!gm_eqbihgc69jv3(a + 0, b + 0)) return false;
    if (!gm_eqk7n1cdvnss(a + 16, b + 16)) return false;
    return true;
}
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)2) return gm_eq2ceic3f8rp(a + 8, b + 8);
    if (ta == (uint8_t)3) return gm_eqrv7v3j3o54(a + 8, b + 8);
    if (ta == (uint8_t)4) return gm_eqpa2btif7eh(a + 8, b + 8);
    if (ta == (uint8_t)5) return gm_eqbdg26q4aml(a + 8, b + 8);
    if (ta == (uint8_t)6) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)7) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)8) return gm_eqv3tj1pu43g(a + 8, b + 8);
    return true;
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_allocator_5m783ja F;
    void * gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = t1;
    void * t3 = (void *)((uint8_t *)gm_cjlj93gp0or);
    gm_t3mf58gv9kf3n t4;
    GM_ST(void *, ((uint8_t *)&t4 + 0), t2);
    GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
}
inline __attribute__((aligned(4))) uint64_t gm_f_column_r91o38o(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_column_r91o38o F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* pos */
    GM_STACK_CHECK_SHALLOW(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
    void * t2 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
    uint64_t t3 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t4 = gm_l1;
    uint64_t t5 = (uintptr_t)t4;
    if (t3 > t5 || t5 > (uintptr_t)t1) gm_trap_slice(t3, t5, t1, gm_srcrchqfd7obmqe, 71, 80);
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(((uint8_t *)t2 + t3 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(t5 - t3));
    gm_b16_8 t7 = gm_f_last_line_hk6q5l4(t6);
    uint64_t t8 = gm_f_char_count_76unqru(t7);
    F.ret = t8;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpi80k4_4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p5lo8ed_3 F;
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
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh_4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_f0gjjbh_4 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lpi80k4_4();
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
inline __attribute__((aligned(4))) void gm_f_list_g3vmkan_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g3vmkan_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_p5lo8ed_3();
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
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_jr5ejs0 F;
    GM_STACK_CHECK(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_node *pn1 = gm_find(cx, 587034433);
    if (!pn1) gm_trap("no handler installed for effect `Alloc`", gm_srcip69en21ht7v, 41, 55);
    gm_t3mf58gv9kf3n t2;
    t2 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn1->clauses[0])(pn1->env, pn1->hctx, pn1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 0, t2);
    GM_ST(void *, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 16, ((void *)0));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 24, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 32, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 40, ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) uint64_t gm_f_out_column_hr4hgng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_out_column_hr4hgng F;
    void * gm_l0; /* out */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    gm_b16_8 t5 = gm_f_last_line_hk6q5l4(t4);
    uint64_t t6 = gm_f_char_count_76unqru(t5);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_f3uajlg_3(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_f3uajlg_3 F;
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
            gm_ttlblounidicj t8;
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
        gm_tsnno3vacsa4v t17 = GM_LD(gm_tsnno3vacsa4v, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 48));
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
        gm_tsnno3vacsa4v t29 = F.l2;
        gm_ttlblounidicj t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 52, 0);
        GM_ST(gm_tsnno3vacsa4v, ((uint8_t *)&t30), t29);
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
inline __attribute__((aligned(4))) uint64_t gm_f_relative_column_ctuu0pd(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_relative_column_ctuu0pd F;
    gm_b16_8 gm_l0; /* line */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 162u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        uint64_t t3 = gm_f_char_count_76unqru(t2);
        gm_l1 = t3;
    }
    {
        gm_b16_8 t4 = gm_l0;
        uint64_t t5 = gm_f_indent_of_i911gpd(t4);
        gm_l2 = t5;
    }
    {
        uint64_t t6;
        uint64_t t8 = gm_l1;
        uint64_t t9 = gm_l2;
        uint8_t t10 = (t8 > t9);
        if (!t10) goto ie7;
        uint64_t t12 = gm_l1;
        uint64_t t13 = gm_l2;
        uint64_t r14; bool o15 = __builtin_sub_overflow(t12, t13, &r14);
        if (o15) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 435, 23);
        uint64_t t16 = r14;
        uint64_t t17 = t16;
        t6 = t17;
        goto ix7;
ie7:;
        uint64_t t19 = ((uint64_t)0ULL);
        t6 = t19;
ix7:;
        uint8_t t20 = 0;
        F.ret = t6;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_non_white_o0cd8c4(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_non_white_o0cd8c4 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_tb31k5l794t7p t8 = GM_CONV(gm_tb31k5l794t7p, gm_f_decode_jrk6b03(t7, ((uint64_t)0ULL)));
        gm_tb31k5l794t7p t9 = t8;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&t9) + 0));
        uint8_t t11 = gm_f_is_white_3njipsh(t10);
        uint8_t t12 = (uint8_t)!t11;
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_aligned_with_s024agj(gm_b16_8 a0, uint64_t a1, uint8_t a2, gm_b16_8 a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_with_s024agj F;
    gm_b16_8 gm_l0; /* line */
    uint64_t gm_l1; /* col */
    uint8_t gm_l2; /* op */
    gm_b16_8 gm_l3; /* text */
    uint64_t gm_l4; /* cut */
    gm_b16_8 gm_l5; /* rest */
    gm_b16_8 gm_l6; /* before */
    uint8_t gm_l7; /* spaced */
    GM_STACK_CHECK_SHALLOW(&F, 361u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_b16_8 t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_b16_8 t4 = gm_l0;
        uint64_t t5 = gm_f_indent_of_i911gpd(t4);
        uint64_t r6; bool o7 = __builtin_add_overflow(t3, t5, &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 185, 37);
        uint64_t t8 = r6;
        uint64_t t9 = gm_f_char_offset_kr8000h(t2, t8);
        gm_l4 = t9;
    }
    {
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t11 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t12 = gm_l4;
        uint64_t t13 = (uintptr_t)t12;
        if (t13 > (uintptr_t)t10 || (uintptr_t)t10 > (uintptr_t)t10) gm_trap_slice(t13, (uintptr_t)t10, t10, gm_srceoep1gquqdsq, 186, 13);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t11 + t13 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)((uintptr_t)t10 - t13));
        gm_l5 = t14;
    }
    {
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t16 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t18 = gm_l4;
        uint64_t t19 = (uintptr_t)t18;
        if (t17 > t19 || t19 > (uintptr_t)t15) gm_trap_slice(t17, t19, t15, gm_srceoep1gquqdsq, 187, 15);
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(((uint8_t *)t16 + t17 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(t19 - t17));
        gm_l6 = t20;
    }
    {
        uint8_t t21 = 0;
        gm_b16_8 t22 = gm_l6;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
        uint8_t t24 = gm_f_ends_with_biriu5l(t22, t23);
        if (t24) {
            gm_b16_8 t25 = gm_l6;
            uint8_t t26 = gm_f_all_white_1b9moim(t25);
            uint8_t t27 = (uint8_t)!t26;
            t21 = t27;
        } else t21 = 0;
        gm_l7 = t21;
    }
    {
        uint8_t t29 = gm_l2;
        if (!t29) goto ie28;
        {
            uint8_t t31 = 0;
            uint8_t t32 = gm_l7;
            if (t32) {
                gm_b16_8 t33 = gm_l5;
                gm_b16_8 t34 = gm_l3;
                uint8_t t35 = gm_f_starts_with_6vvt01e(t33, t34);
                t31 = t35;
            } else t31 = 0;
            uint8_t t36 = 0;
            F.ret = t31;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        uint8_t t37 = 0;
        uint8_t t38 = gm_l7;
        if (t38) {
            gm_b16_8 t39 = gm_l5;
            uint8_t t40 = gm_f_starts_with_non_white_o0cd8c4(t39);
            t37 = t40;
        } else t37 = 0;
        uint8_t t41 = 0;
        F.ret = t37;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_continues_line_2cliit4(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_continues_line_2cliit4 F;
    gm_b16_8 gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 111u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = 0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 34));
        if (!t4) {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 35));
            t3 = t5;
        } else t3 = 1;
        if (!t3) goto ie2;
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
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 16));
        gm_l1 = t8;
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sgpdmvc9s0d2p, 2) == 0)) goto pa12;
        goto po11;
pa12:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_svbhcvlahd4n2, 2) == 0)) goto pa13;
        goto po11;
pa13:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s920dpvvv6cej, 3) == 0)) goto pa14;
        goto po11;
pa14:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_spi47lg4tr80j, 2) == 0)) goto pa15;
        goto po11;
pa15:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s9es2tos1bhfp, 4) == 0)) goto pa16;
        goto po11;
pa16:;
        goto mn10;
po11:;
        uint8_t t17 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me9;
mn10:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sc8o4oaph9gbr, 1) == 0)) goto pa20;
        goto po19;
pa20:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_spod3tmkh25up, 1) == 0)) goto pa21;
        goto po19;
pa21:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sfqk3rro4g7t0, 1) == 0)) goto pa22;
        goto po19;
pa22:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sf9a2kg3eo1gs, 1) == 0)) goto pa23;
        goto po19;
pa23:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_srs1vrn2uaams, 1) == 0)) goto pa24;
        goto po19;
pa24:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sfi2ha42r1nr2, 1) == 0)) goto pa25;
        goto po19;
pa25:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_seccpktgmrevt, 1) == 0)) goto pa26;
        goto po19;
pa26:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_soqt3jnmovq5l, 3) == 0)) goto pa27;
        goto po19;
pa27:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sjfg2291kjugh, 3) == 0)) goto pa28;
        goto po19;
pa28:;
        goto mn18;
po19:;
        uint8_t t29 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me9;
mn18:;
        goto me9;
mn30:;
        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 143, 5);
me9:;
    }
    {
        uint8_t t32 = 0;
        uint8_t t33 = 0;
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sccjki3qgi8m7));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(14));
        uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_srceoep1gquqdsq, 148, 39);
        uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t35 * 1));
        gm_tf5srh4mp4b3i t38 = gm_f_index_of_hdeg351(t34, t37);
        gm_tf5srh4mp4b3i t39 = t38;
        uint8_t t40;
        if (((((uint8_t *)&t39))[8] != 0)) goto mn42;
        t40 = ((uint8_t)0);
        goto me41;
mn42:;
        t40 = ((uint8_t)1);
        goto me41;
mn43:;
        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 148, 12);
me41:;
        if (!t40) {
            uint8_t t44 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s1c4sfff8ac39, 6) == 0));
            t33 = t44;
        } else t33 = 1;
        if (!t33) {
            uint8_t t45 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sidtn67sdk4e0, 6) == 0));
            t32 = t45;
        } else t32 = 1;
        uint8_t t46 = 0;
        F.ret = t32;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_continues_line_2cliit4_c(gm_t88jm1dgsmkjs a0) { return gm_f_continues_line_2cliit4(&a0); }
inline __attribute__((aligned(4))) uint8_t gm_f_continues_with_name_ejtvq2k(gm_b16_8 a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_continues_with_name_ejtvq2k F;
    gm_b16_8 gm_l0; /* src */
    uint8_t gm_l2; /* c */
    uint8_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = (!((GM_LD(uintptr_t, (((uint8_t *)a1) + 16) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)a1) + 16)), gm_sl3q52ep6aa53, 1) == 0));
        if (!t3) goto ie2;
        {
            uint8_t t5 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = t6;
        void * t8 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t7)));
        uint64_t t9 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t7) + 8);
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        uint64_t t11 = (uintptr_t)t10;
        gm_tqs5p4n2tq19v t12;
        if (t11 < t9) {
        uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)t8 + t11 * 1));
        gm_tqs5p4n2tq19v t14;
        GM_ST(uint8_t, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[1] = 1;
            t12 = t14;
        } else {
        gm_tqs5p4n2tq19v t15;
        memset(&t15, 0, sizeof t15);
            t12 = t15;
        }
        gm_tqs5p4n2tq19v t16 = t12;
        uint8_t t17;
        if (((((uint8_t *)&t16))[1] != 0)) {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
            t17 = t18;
        } else {
            uint8_t t19 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l2 = t17;
    }
    {
        uint8_t t20 = 0;
        uint8_t t21 = 0;
        uint8_t t22 = 0;
        {
            uint8_t t24 = gm_l2;
            gm_l3 = t24;
        }
        uint8_t t25 = 0;
        uint8_t t26 = gm_l3;
        uint8_t t27 = (((uint8_t)97ULL) <= t26);
        if (t27) {
            uint8_t t28 = gm_l3;
            uint8_t t29 = (t28 <= ((uint8_t)122ULL));
            t25 = t29;
        } else t25 = 0;
        uint8_t t30 = t25;
        if (!t30) {
            {
                uint8_t t32 = gm_l2;
                gm_l4 = t32;
            }
            uint8_t t33 = 0;
            uint8_t t34 = gm_l4;
            uint8_t t35 = (((uint8_t)65ULL) <= t34);
            if (t35) {
                uint8_t t36 = gm_l4;
                uint8_t t37 = (t36 <= ((uint8_t)90ULL));
                t33 = t37;
            } else t33 = 0;
            uint8_t t38 = t33;
            t22 = t38;
        } else t22 = 1;
        if (!t22) {
            uint8_t t39 = gm_l2;
            uint8_t t40 = ((t39 == ((uint8_t)95ULL)));
            t21 = t40;
        } else t21 = 1;
        if (t21) {
            uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t42 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
            uint64_t t44 = (uintptr_t)t43;
            if (t44 > (uintptr_t)t41 || (uintptr_t)t41 > (uintptr_t)t41) gm_trap_slice(t44, (uintptr_t)t41, t41, gm_srceoep1gquqdsq, 136, 85);
            gm_b16_8 t45;
            GM_ST(void *, (uint8_t *)&t45, (void *)(((uint8_t *)t42 + t44 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)((uintptr_t)t41 - t44));
            uint8_t t46 = gm_f_line_binds_jm0pnul(t45);
            uint8_t t47 = (uint8_t)!t46;
            t20 = t47;
        } else t20 = 0;
        uint8_t t48 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_continues_with_name_ejtvq2k_c(gm_b16_8 a0, gm_t88jm1dgsmkjs a1) { return gm_f_continues_with_name_ejtvq2k(a0, &a1); }
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_char_3bc3hom(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_punct_char_3bc3hom F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    gm_ta72bg8o6meed t2;
    GM_ST(uint8_t, (uint8_t *)&t2 + 0, ((uint8_t)45ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 1, ((uint8_t)43ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 2, ((uint8_t)42ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 3, ((uint8_t)47ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 4, ((uint8_t)37ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 5, ((uint8_t)60ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 6, ((uint8_t)62ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 7, ((uint8_t)61ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 8, ((uint8_t)33ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 9, ((uint8_t)38ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 10, ((uint8_t)124ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 11, ((uint8_t)94ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 12, ((uint8_t)126ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 13, ((uint8_t)46ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 14, ((uint8_t)58ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 15, ((uint8_t)63ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 16, ((uint8_t)64ULL));
    GM_ST(uint8_t, (uint8_t *)&t2 + 17, ((uint8_t)35ULL));
    gm_ta72bg8o6meed t3 = t2;
    uint8_t t4 = 0;
    uint8_t t5 = t1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 0, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 1, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 2, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 3, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 4, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 5, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 6, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 7, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 8, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 9, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 10, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 11, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 12, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 13, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 14, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 15, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 16, ((uint8_t *)&t5))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 17, ((uint8_t *)&t5))) t4 = 1;
    uint8_t t6 = (uint8_t)(t4);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_word_kb7cdmu(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_word_kb7cdmu F;
    uint8_t gm_l0; /* c */
    uint8_t gm_l1; /* <tmp> */
    uint8_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint8_t t3 = 0;
    {
        uint8_t t5 = gm_l0;
        gm_l1 = t5;
    }
    uint8_t t6 = 0;
    uint8_t t7 = gm_l1;
    uint8_t t8 = (((uint8_t)97ULL) <= t7);
    if (t8) {
        uint8_t t9 = gm_l1;
        uint8_t t10 = (t9 <= ((uint8_t)122ULL));
        t6 = t10;
    } else t6 = 0;
    uint8_t t11 = t6;
    if (!t11) {
        {
            uint8_t t13 = gm_l0;
            gm_l2 = t13;
        }
        uint8_t t14 = 0;
        uint8_t t15 = gm_l2;
        uint8_t t16 = (((uint8_t)65ULL) <= t15);
        if (t16) {
            uint8_t t17 = gm_l2;
            uint8_t t18 = (t17 <= ((uint8_t)90ULL));
            t14 = t18;
        } else t14 = 0;
        uint8_t t19 = t14;
        t3 = t19;
    } else t3 = 1;
    if (!t3) {
        {
            uint8_t t21 = gm_l0;
            gm_l3 = t21;
        }
        uint8_t t22 = 0;
        uint8_t t23 = gm_l3;
        uint8_t t24 = (((uint8_t)48ULL) <= t23);
        if (t24) {
            uint8_t t25 = gm_l3;
            uint8_t t26 = (t25 <= ((uint8_t)57ULL));
            t22 = t26;
        } else t22 = 0;
        uint8_t t27 = t22;
        t2 = t27;
    } else t2 = 1;
    if (!t2) {
        uint8_t t28 = gm_l0;
        gm_t2abnvcnt53ot t29;
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)95ULL));
        GM_ST(uint8_t, ((uint8_t *)&t29 + 1), ((uint8_t)34ULL));
        GM_ST(uint8_t, ((uint8_t *)&t29 + 2), ((uint8_t)39ULL));
        GM_ST(uint8_t, ((uint8_t *)&t29 + 3), ((uint8_t)96ULL));
        gm_t2abnvcnt53ot t30 = t29;
        uint8_t t31 = 0;
        uint8_t t32 = t28;
        if (gm_eqjmccqdb90f(((uint8_t *)&t30) + 0, ((uint8_t *)&t32))) t31 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t30) + 1, ((uint8_t *)&t32))) t31 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t30) + 2, ((uint8_t *)&t32))) t31 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t30) + 3, ((uint8_t *)&t32))) t31 = 1;
        uint8_t t33 = (uint8_t)(t31);
        t1 = t33;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_keeps_padding_m0v49h1(gm_b16_8 a0, const void * a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_keeps_padding_m0v49h1 F;
    gm_b16_8 gm_l0; /* src */
    uint8_t gm_l3; /* op */
    uint64_t gm_l4; /* start */
    uint64_t gm_l5; /* end */
    gm_th2lqbo77pvsc gm_l6; /* above */
    gm_th2lqbo77pvsc gm_l7; /* below */
    gm_b16_8 gm_l8; /* rest */
    gm_b16_8 gm_l9; /* own */
    uint64_t gm_l10; /* own_indent */
    uint64_t gm_l11; /* here */
    uint64_t gm_l12; /* col */
    gm_b16_8 gm_l13; /* line */
    gm_b16_8 gm_l14; /* line */
    GM_STACK_CHECK_SHALLOW(&F, 1035u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 16));
        gm_t6j6smj7qoctu t3;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_ss67f0gmfen1m));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 0, t4);
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sm8obe455p2d9));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 16, t5);
        gm_t6j6smj7qoctu t6 = t3;
        uint8_t t7 = 0;
        gm_b16_8 t8 = t2;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t6) + 0, ((uint8_t *)&t8))) t7 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t6) + 16, ((uint8_t *)&t8))) t7 = 1;
        uint8_t t9 = (uint8_t)(t7);
        gm_l3 = t9;
    }
    {
        uint8_t t11 = gm_l3;
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie10;
        uint8_t t13 = (!((GM_LD(uintptr_t, (((uint8_t *)a1) + 16) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)a1) + 16)), gm_s39mljh669qgl, 1) == 0));
        if (!t13) goto ie10;
        {
            uint8_t t15 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        gm_b16_8 t16 = gm_l0;
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)a2) + 0));
        uint64_t t18 = gm_f_line_start_pl10jt5(t16, t17);
        gm_l4 = t18;
    }
    {
        gm_b16_8 t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)a2) + 0));
        uint64_t t21 = gm_f_line_end_jm825k5(t19, t20);
        gm_l5 = t21;
    }
    {
        gm_th2lqbo77pvsc t22;
        memset(&t22, 0, sizeof t22);
        gm_l6 = t22;
    }
    {
        uint64_t t24 = gm_l4;
        uint8_t t25 = (t24 > ((uint64_t)0ULL));
        if (!t25) goto ie23;
        {
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t28 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t29 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t30 = gm_l4;
            uint64_t r31; bool o32 = __builtin_sub_overflow(t30, ((uint64_t)1ULL), &r31);
            if (o32) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 163, 45);
            uint64_t t33 = r31;
            uint64_t t34 = (uintptr_t)t33;
            if (t29 > t34 || t34 > (uintptr_t)t27) gm_trap_slice(t29, t34, t27, gm_srceoep1gquqdsq, 163, 38);
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(((uint8_t *)t28 + t29 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(t34 - t29));
            gm_b16_8 t36 = gm_f_last_line_hk6q5l4(t35);
            gm_th2lqbo77pvsc t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t37), t36);
            ((uint8_t *)&t37)[16] = 1;
            gm_l6 = t37;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        gm_th2lqbo77pvsc t38;
        memset(&t38, 0, sizeof t38);
        gm_l7 = t38;
    }
    {
        uint64_t t40 = gm_l5;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t42 = (t40 < t41);
        if (!t42) goto ie39;
        {
            uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t45 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t46 = gm_l5;
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
            if (o48) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 166, 21);
            uint64_t t49 = r47;
            uint64_t t50 = (uintptr_t)t49;
            if (t50 > (uintptr_t)t44 || (uintptr_t)t44 > (uintptr_t)t44) gm_trap_slice(t50, (uintptr_t)t44, t44, gm_srceoep1gquqdsq, 166, 17);
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(((uint8_t *)t45 + t50 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)((uintptr_t)t44 - t50));
            gm_l8 = t51;
        }
        {
            gm_b16_8 t52 = gm_l8;
            gm_b16_8 t53 = t52;
            uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&t53) + 8);
            void * t55 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t53)));
            uint64_t t56 = (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t57 = gm_l8;
            uint64_t t58 = gm_f_line_end_jm825k5(t57, ((uint64_t)0ULL));
            uint64_t t59 = (uintptr_t)t58;
            if (t56 > t59 || t59 > (uintptr_t)t54) gm_trap_slice(t56, t59, t54, gm_srceoep1gquqdsq, 167, 17);
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(((uint8_t *)t55 + t56 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(t59 - t56));
            gm_th2lqbo77pvsc t61;
            GM_ST(uint8_t, (uint8_t *)&t61 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t61 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t61), t60);
            ((uint8_t *)&t61)[16] = 1;
            gm_l7 = t61;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t63 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t64 = gm_l4;
        uint64_t t65 = (uintptr_t)t64;
        uint64_t t66 = gm_l5;
        uint64_t t67 = (uintptr_t)t66;
        if (t65 > t67 || t67 > (uintptr_t)t62) gm_trap_slice(t65, t67, t62, gm_srceoep1gquqdsq, 169, 12);
        gm_b16_8 t68;
        GM_ST(void *, (uint8_t *)&t68, (void *)(((uint8_t *)t63 + t65 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(t67 - t65));
        gm_l9 = t68;
    }
    {
        gm_b16_8 t69 = gm_l9;
        uint64_t t70 = gm_f_indent_of_i911gpd(t69);
        gm_l10 = t70;
    }
    {
        gm_b16_8 t71 = gm_l0;
        uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)a2) + 0));
        uint64_t t73 = gm_f_column_r91o38o(t71, t72);
        gm_l11 = t73;
    }
    {
        uint64_t t74;
        uint64_t t76 = gm_l11;
        uint64_t t77 = gm_l10;
        uint8_t t78 = (t76 > t77);
        if (!t78) goto ie75;
        uint64_t t80 = gm_l11;
        uint64_t t81 = gm_l10;
        uint64_t r82; bool o83 = __builtin_sub_overflow(t80, t81, &r82);
        if (o83) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 172, 35);
        uint64_t t84 = r82;
        uint64_t t85 = t84;
        t74 = t85;
        goto ix75;
ie75:;
        uint64_t t87 = ((uint64_t)0ULL);
        t74 = t87;
ix75:;
        gm_l12 = t74;
    }
    {
        if (!((((uint8_t *)&gm_l6))[16] != 0)) goto ie88;
        gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
        gm_l13 = t89;
        {
            gm_b16_8 t92 = gm_l13;
            uint64_t t93 = gm_l12;
            uint8_t t94 = gm_l3;
            gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 16));
            uint8_t t96 = gm_f_aligned_with_s024agj(t92, t93, t94, t95);
            if (!t96) goto ie91;
            {
                uint8_t t98 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix91;
ie91:;
ix91:;
        }
        goto ix88;
ie88:;
ix88:;
    }
    {
        if (!((((uint8_t *)&gm_l7))[16] != 0)) goto ie99;
        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
        gm_l14 = t100;
        {
            gm_b16_8 t103 = gm_l14;
            uint64_t t104 = gm_l12;
            uint8_t t105 = gm_l3;
            gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 16));
            uint8_t t107 = gm_f_aligned_with_s024agj(t103, t104, t105, t106);
            if (!t107) goto ie102;
            {
                uint8_t t109 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix102;
ie102:;
ix102:;
        }
        goto ix99;
ie99:;
ix99:;
    }
    {
        uint8_t t110 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_keeps_padding_m0v49h1_c(gm_b16_8 a0, gm_t88jm1dgsmkjs a1, gm_t88jm1dgsmkjs a2) { return gm_f_keeps_padding_m0v49h1(a0, &a1, &a2); }
__attribute__((aligned(4))) void gm_f_layout_ufvu3cq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_layout_ufvu3cq F;
    gm_b16_8 gm_l0; /* src */
    gm_b16_8 gm_l1; /* toks */
    uint8_t gm_l3; /* bom */
    uint64_t gm_l4; /* base */
    uint64_t gm_l6; /* prev_end */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* k */
    uint8_t gm_l12; /* line_open */
    uint64_t gm_l13; /* nl */
    gm_t8c5h7u09n10a gm_l14; /* prev */
    uint64_t gm_l16; /* cur_base */
    uint64_t gm_l17; /* min_len */
    uint8_t gm_l18; /* cont_next */
    uint8_t gm_l19; /* after_lbrace */
    gm_tf5srh4mp4b3i gm_l20; /* line_head */
    uint8_t gm_l21; /* after_chain */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* idx */
    uint8_t gm_l26; /* close_tok */
    uint8_t gm_l27; /* close_brace */
    uint64_t gm_l28; /* k */
    uint8_t gm_l29; /* trailing */
    uint8_t gm_l30; /* first */
    gm_tf5srh4mp4b3i gm_l31; /* head */
    uint64_t gm_l32; /* indent */
    uint64_t gm_l34; /* v */
    uint8_t gm_l36; /* plain_ctx */
    uint8_t gm_l37; /* starts_cont */
    uint8_t gm_l38; /* is_else */
    uint64_t gm_l39; /* h */
    uint8_t gm_l40; /* chain_else */
    uint8_t gm_l41; /* continues */
    gm_tf5srh4mp4b3i gm_l42; /* aligned */
    gm_t2lqu8jsj511d gm_l44; /* v */
    uint64_t gm_l45; /* h */
    uint64_t gm_l46; /* level */
    uint64_t gm_l47; /* col */
    uint8_t gm_l49; /* lbrace */
    gm_t6j6smj7qoctu gm_l50; /* l */
    gm_b16_8 gm_l51; /* text */
    uint8_t gm_l52; /* prev_ta */
    uint64_t gm_l53; /* p */
    uint64_t gm_l54; /* k */
    uint64_t gm_l56; /* pad */
    uint8_t gm_l57; /* space */
    uint64_t gm_l58; /* p */
    uint8_t gm_l60; /* tight */
    gm_b16_8 gm_l61; /* gap */
    uint64_t gm_l62; /* min_gap */
    uint64_t gm_l63; /* want */
    uint64_t gm_l64; /* have */
    uint8_t gm_l65; /* glued */
    uint64_t gm_l66; /* p */
    void * gm_l67; /* f */
    uint8_t gm_l68; /* brace */
    uint8_t gm_l69; /* chain */
    uint64_t gm_l70; /* h */
    GM_STACK_CHECK(&F, 4182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_f_classify_e96658j((void *)&F.l2, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        gm_b16_8 t5 = gm_l0;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_srh29phg8b30r));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(3));
        uint8_t t7 = gm_f_starts_with_6vvt01e(t5, t6);
        gm_l3 = t7;
    }
    {
        uint64_t t8;
        uint8_t t10 = gm_l3;
        if (!t10) goto ie9;
        uint64_t t12 = ((uint64_t)3ULL);
        t8 = t12;
        goto ix9;
ie9:;
        uint64_t t14 = ((uint64_t)0ULL);
        t8 = t14;
ix9:;
        gm_l4 = t8;
    }
    {
        gm_f_list_f0gjjbh_4((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
    }
    {
        uint64_t t16 = gm_l4;
        gm_l6 = t16;
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            gm_l7 = t18;
        }
        {
            for (;;) {
                uint64_t t20 = gm_l8;
                uint64_t t21 = gm_l7;
                uint8_t t22 = (t20 < t21);
                if (!t22) goto lb19;
                {
                    uint64_t t25 = gm_l8;
                    uint64_t t26 = t25;
                    uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&t26));
                    gm_l9 = t29;
                    {
                        uint64_t t31 = gm_l9;
                        uint64_t t32 = (uintptr_t)t31;
                        uint64_t t33 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                        if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srceoep1gquqdsq, 234, 17);
                        gm_t88jm1dgsmkjs t34 = GM_LD(gm_t88jm1dgsmkjs, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t32 * 40));
                        F.l10 = t34;
                    }
                    {
                        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        void * t36 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                        uint64_t t37 = gm_l6;
                        uint64_t t38 = (uintptr_t)t37;
                        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 0));
                        uint64_t t40 = (uintptr_t)t39;
                        if (t38 > t40 || t40 > (uintptr_t)t35) gm_trap_slice(t38, t40, t35, gm_srceoep1gquqdsq, 235, 18);
                        gm_b16_8 t41;
                        GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t36 + t38 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t38));
                        void * t42 = (void *)((uint8_t *)&F.l5);
                        gm_b0_1 t43 = gm_f_scan_gap_tkq0f9l((void *)NULL, cx, NULL, t41, t42);
                        if (gm_unw.kind) {
                            goto gm_ll23;
                        }
                    }
                    {
                        void * t44 = (void *)((uint8_t *)&F.l5);
                        uint64_t t45 = gm_l9;
                        gm_tj63kbqqj2092 t46;
                        GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t46 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t46 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t46 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)0ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t46 + 8), t45);
                        gm_tftb1odc739t5 t47 = gm_f_push_1e500nc_4(t44, &t46);
                        gm_tftb1odc739t5 t48 = t47;
                        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srceoep1gquqdsq, 236, 9);
                        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                    }
                    {
                        uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                        gm_l6 = t50;
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 233, 5);
me27:;
                }
lc19:;
                {
                    uint64_t t52 = gm_l8;
                    uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                    uint64_t t55 = r53;
                    gm_l8 = t55;
                }
            }
gm_ll23:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
            }
            goto gm_ld1_2;
lb19:;
        }
    }
    {
        uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t57 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t58 = gm_l6;
        uint64_t t59 = (uintptr_t)t58;
        if (t59 > (uintptr_t)t56 || (uintptr_t)t56 > (uintptr_t)t56) gm_trap_slice(t59, (uintptr_t)t56, t56, gm_srceoep1gquqdsq, 239, 14);
        gm_b16_8 t60;
        GM_ST(void *, (uint8_t *)&t60, (void *)(((uint8_t *)t57 + t59 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)((uintptr_t)t56 - t59));
        void * t61 = (void *)((uint8_t *)&F.l5);
        gm_b0_1 t62 = gm_f_scan_gap_tkq0f9l((void *)NULL, cx, NULL, t60, t61);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
        uint8_t t64 = gm_l3;
        if (!t64) goto ie63;
        {
            void * t66 = (void *)((uint8_t *)&F.l11);
            gm_b16_8 t67;
            GM_ST(void *, (uint8_t *)&t67, (void *)(gm_srh29phg8b30r));
            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(3));
            gm_tftb1odc739t5 t68 = gm_f_extend_0cs85gs(t66, t67);
            gm_tftb1odc739t5 t69 = t68;
            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_srceoep1gquqdsq, 242, 14);
            gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
        }
        goto ix63;
ie63:;
ix63:;
    }
    {
        gm_l12 = ((uint8_t)0);
    }
    {
        gm_l13 = ((uint64_t)0ULL);
    }
    {
        gm_t8c5h7u09n10a t71;
        GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t71 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)0ULL));
        gm_l14 = t71;
    }
    {
        gm_f_list_g3vmkan_3((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
    }
    {
        gm_l16 = ((uint64_t)0ULL);
    }
    {
        gm_l17 = ((uint64_t)0ULL);
    }
    {
        gm_l18 = ((uint8_t)0);
    }
    {
        gm_l19 = ((uint8_t)0);
    }
    {
        gm_tf5srh4mp4b3i t73;
        memset(&t73, 0, sizeof t73);
        gm_l20 = t73;
    }
    {
        gm_l21 = ((uint8_t)0);
    }
    {
        {
            gm_l23 = ((uint64_t)0ULL);
        }
        {
            uint64_t t75 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            gm_l22 = t75;
        }
        {
            for (;;) {
                uint64_t t77 = gm_l23;
                uint64_t t78 = gm_l22;
                uint8_t t79 = (t77 < t78);
                if (!t79) goto lb76;
                {
                    uint64_t t82 = gm_l23;
                    uint64_t t83 = t82;
                    uint64_t t86 = GM_LD(uint64_t, ((uint8_t *)&t83));
                    gm_l24 = t86;
                    {
                        uint64_t t88 = gm_l24;
                        uint64_t t89 = (uintptr_t)t88;
                        uint64_t t90 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                        if (t89 >= (uintptr_t)(t90)) gm_trap_index(t89, t90, gm_srceoep1gquqdsq, 263, 17);
                        gm_tj63kbqqj2092 t91 = GM_LD(gm_tj63kbqqj2092, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t89 * 40));
                        F.l25 = t91;
                    }
                    {
                        uint8_t t93 = ((GM_LD(uint8_t, ((uint8_t *)&F.l25) + 0) == ((uint8_t)2ULL)));
                        if (!t93) goto ie92;
                        {
                            uint64_t t95 = gm_l13;
                            uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                            if (o97) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 265, 13);
                            uint64_t t98 = r96;
                            gm_l13 = t98;
                        }
                        {
                            goto lc76;
                        }
                        goto ix92;
ie92:;
ix92:;
                    }
                    {
                        gm_l26 = ((uint8_t)0);
                    }
                    {
                        gm_l27 = ((uint8_t)0);
                    }
                    {
                        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 0));
                        if (t100 != ((uint8_t)0ULL)) goto ie99;
                        uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 8));
                        gm_l28 = t101;
                        {
                            uint64_t t103 = gm_l28;
                            uint64_t t104 = (uintptr_t)t103;
                            uint64_t t105 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                            if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srceoep1gquqdsq, 271, 25);
                            uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t104 * 40) + 35));
                            gm_l26 = t106;
                        }
                        {
                            uint8_t t107 = 0;
                            uint8_t t108 = gm_l26;
                            if (t108) {
                                uint64_t t109 = gm_l28;
                                uint64_t t110 = (uintptr_t)t109;
                                uint64_t t111 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                if (t110 >= (uintptr_t)(t111)) gm_trap_index(t110, t111, gm_srceoep1gquqdsq, 272, 41);
                                uint8_t t112 = (((GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t110 * 40) + 16) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t110 * 40) + 16)), gm_s2cuflhqkkuaq, 1) == 0));
                                t107 = t112;
                            } else t107 = 0;
                            gm_l27 = t107;
                        }
                        goto ix99;
ie99:;
ix99:;
                    }
                    {
                        uint8_t t113 = 0;
                        uint8_t t114 = 0;
                        uint8_t t115 = gm_l12;
                        if (t115) {
                            uint64_t t116 = gm_l13;
                            uint8_t t117 = ((t116 == ((uint64_t)0ULL)));
                            t114 = t117;
                        } else t114 = 0;
                        if (t114) {
                            uint8_t t118;
                            uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 0));
                            if (t121 != ((uint8_t)3ULL)) goto mn120;
                            t118 = ((uint8_t)1);
                            goto me119;
mn120:;
                            t118 = ((uint8_t)0);
                            goto me119;
mn122:;
                            gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 274, 47);
me119:;
                            t113 = t118;
                        } else t113 = 0;
                        gm_l29 = t113;
                    }
                    {
                        uint64_t t124 = gm_l13;
                        uint8_t t125 = (t124 > ((uint64_t)0ULL));
                        if (!t125) goto ie123;
                        uint8_t t126 = gm_l12;
                        if (!t126) goto ie123;
                        {
                            void * t128 = (void *)((uint8_t *)&F.l11);
                            gm_tftb1odc739t5 t129 = gm_f_push_t83rg2m(t128, ((uint8_t)10ULL));
                            gm_tftb1odc739t5 t130 = t129;
                            if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 1, gm_srceoep1gquqdsq, 276, 13);
                            gm_b0_1 t131 = GM_LD(gm_b0_1, (((uint8_t *)&t130) + 1));
                        }
                        {
                            uint64_t t133 = gm_l13;
                            uint8_t t134 = (t133 >= ((uint64_t)2ULL));
                            if (!t134) goto ie132;
                            uint8_t t135 = gm_l19;
                            uint8_t t136 = (uint8_t)!t135;
                            if (!t136) goto ie132;
                            uint8_t t137 = gm_l27;
                            uint8_t t138 = (uint8_t)!t137;
                            if (!t138) goto ie132;
                            {
                                void * t140 = (void *)((uint8_t *)&F.l11);
                                gm_tftb1odc739t5 t141 = gm_f_push_t83rg2m(t140, ((uint8_t)10ULL));
                                gm_tftb1odc739t5 t142 = t141;
                                if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 1, gm_srceoep1gquqdsq, 277, 67);
                                gm_b0_1 t143 = GM_LD(gm_b0_1, (((uint8_t *)&t142) + 1));
                            }
                            goto ix132;
ie132:;
ix132:;
                        }
                        {
                            gm_l12 = ((uint8_t)0);
                        }
                        goto ix123;
ie123:;
ix123:;
                    }
                    {
                        gm_l13 = ((uint64_t)0ULL);
                    }
                    {
                        uint8_t t144 = gm_l12;
                        uint8_t t145 = (uint8_t)!t144;
                        gm_l30 = t145;
                    }
                    {
                        uint8_t t147 = gm_l30;
                        if (!t147) goto ie146;
                        {
                            gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                            gm_b16_8 t150 = t149;
                            gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                            uint64_t t152 = gm_l24;
                            gm_tf5srh4mp4b3i t153 = gm_f_next_tok_qap88op(t151, t152);
                            gm_l31 = t153;
                        }
                        {
                            gm_tf5srh4mp4b3i t154 = gm_l31;
                            gm_l20 = t154;
                        }
                        {
                            gm_l32 = ((uint64_t)0ULL);
                        }
                        {
                            uint8_t t156 = gm_l26;
                            if (!t156) goto ie155;
                            {
                                void * t158 = (void *)((uint8_t *)&F.l15);
                                gm_f_pop_f3uajlg_3((void *)&F.l33, t158);
                            }
                            {
                                gm_l21 = ((uint8_t)0);
                            }
                            {
                                gm_l34 = ((uint64_t)0ULL);
                            }
                            {
                                if (!((((uint8_t *)&F.l33))[48] != 0)) goto ie159;
                                gm_tsnno3vacsa4v t160 = GM_LD(gm_tsnno3vacsa4v, ((uint8_t *)&F.l33));
                                F.l35 = t160;
                                {
                                    uint8_t t162 = 0;
                                    uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 40));
                                    if (t163) {
                                        uint8_t t164 = gm_l27;
                                        t162 = t164;
                                    } else t162 = 0;
                                    gm_l21 = t162;
                                }
                                {
                                    uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)&F.l35) + 0));
                                    gm_l34 = t165;
                                }
                                goto ix159;
ie159:;
ix159:;
                            }
                            {
                                uint64_t t166 = gm_l34;
                                gm_l16 = t166;
                            }
                            {
                                uint64_t t167 = gm_l34;
                                uint64_t r168; bool o169 = __builtin_mul_overflow(t167, ((uint64_t)4ULL), &r168);
                                if (o169) gm_trap("integer overflow in `*`", gm_srceoep1gquqdsq, 295, 26);
                                uint64_t t170 = r168;
                                gm_l32 = t170;
                            }
                            goto ix155;
ie155:;
                            {
                                uint64_t t172;
                                uint64_t t174 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                uint8_t t175 = (t174 > ((uint64_t)0ULL));
                                if (!t175) goto ie173;
                                uint64_t t177 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                uint64_t r178; bool o179 = __builtin_sub_overflow(t177, ((uint64_t)1ULL), &r178);
                                if (o179) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 297, 65);
                                uint64_t t180 = r178;
                                uint64_t t181 = (uintptr_t)t180;
                                uint64_t t182 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                if (t181 >= (uintptr_t)(t182)) gm_trap_index(t181, t182, gm_srceoep1gquqdsq, 297, 53);
                                uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t181 * 48) + 0));
                                uint64_t r184; bool o185 = __builtin_add_overflow(t183, ((uint64_t)1ULL), &r184);
                                if (o185) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 297, 53);
                                uint64_t t186 = r184;
                                uint64_t t187 = t186;
                                t172 = t187;
                                goto ix173;
ie173:;
                                uint64_t t189 = ((uint64_t)0ULL);
                                t172 = t189;
ix173:;
                                gm_l16 = t172;
                            }
                            {
                                uint8_t t190 = 0;
                                uint64_t t191 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                uint8_t t192 = ((t191 == ((uint64_t)0ULL)));
                                if (!t192) {
                                    uint64_t t193 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                    uint64_t r194; bool o195 = __builtin_sub_overflow(t193, ((uint64_t)1ULL), &r194);
                                    if (o195) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 298, 66);
                                    uint64_t t196 = r194;
                                    uint64_t t197 = (uintptr_t)t196;
                                    uint64_t t198 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                    if (t197 >= (uintptr_t)(t198)) gm_trap_index(t197, t198, gm_srceoep1gquqdsq, 298, 54);
                                    uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t197 * 48) + 8));
                                    t190 = t199;
                                } else t190 = 1;
                                gm_l36 = t190;
                            }
                            {
                                gm_l37 = ((uint8_t)0);
                            }
                            {
                                gm_l38 = ((uint8_t)0);
                            }
                            {
                                if (!((((uint8_t *)&gm_l31))[8] != 0)) goto ie200;
                                uint64_t t201 = GM_LD(uint64_t, ((uint8_t *)&gm_l31));
                                gm_l39 = t201;
                                {
                                    uint8_t t203 = 0;
                                    uint64_t t204 = gm_l39;
                                    uint64_t t205 = (uintptr_t)t204;
                                    uint64_t t206 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                    if (t205 >= (uintptr_t)(t206)) gm_trap_index(t205, t206, gm_srceoep1gquqdsq, 302, 35);
                                    gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t205 * 40) + 16));
                                    gm_tfeab279c16rr t208;
                                    gm_b16_8 t209;
                                    GM_ST(void *, (uint8_t *)&t209, (void *)(gm_s920dpvvv6cej));
                                    GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(3));
                                    GM_ST(gm_b16_8, (uint8_t *)&t208 + 0, t209);
                                    gm_b16_8 t210;
                                    GM_ST(void *, (uint8_t *)&t210, (void *)(gm_spi47lg4tr80j));
                                    GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(2));
                                    GM_ST(gm_b16_8, (uint8_t *)&t208 + 16, t210);
                                    gm_b16_8 t211;
                                    GM_ST(void *, (uint8_t *)&t211, (void *)(gm_s9es2tos1bhfp));
                                    GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(4));
                                    GM_ST(gm_b16_8, (uint8_t *)&t208 + 32, t211);
                                    gm_tfeab279c16rr t212 = t208;
                                    uint8_t t213 = 0;
                                    gm_b16_8 t214 = t207;
                                    if (gm_eq0iqiubfm7e(((uint8_t *)&t212) + 0, ((uint8_t *)&t214))) t213 = 1;
                                    if (gm_eq0iqiubfm7e(((uint8_t *)&t212) + 16, ((uint8_t *)&t214))) t213 = 1;
                                    if (gm_eq0iqiubfm7e(((uint8_t *)&t212) + 32, ((uint8_t *)&t214))) t213 = 1;
                                    uint8_t t215 = (uint8_t)(t213);
                                    if (!t215) {
                                        gm_b16_8 t216 = gm_l0;
                                        uint64_t t217 = gm_l39;
                                        uint64_t t218 = (uintptr_t)t217;
                                        uint64_t t219 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                        if (t218 >= (uintptr_t)(t219)) gm_trap_index(t218, t219, gm_srceoep1gquqdsq, 302, 108);
                                        uint8_t t220 = gm_f_continues_with_name_ejtvq2k(t216, &(*(gm_t88jm1dgsmkjs *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t218 * 40)));
                                        t203 = t220;
                                    } else t203 = 1;
                                    gm_l37 = t203;
                                }
                                {
                                    uint64_t t221 = gm_l39;
                                    uint64_t t222 = (uintptr_t)t221;
                                    uint64_t t223 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                    if (t222 >= (uintptr_t)(t223)) gm_trap_index(t222, t223, gm_srceoep1gquqdsq, 303, 31);
                                    uint8_t t224 = (((GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t222 * 40) + 16) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t222 * 40) + 16)), gm_s9es2tos1bhfp, 4) == 0));
                                    gm_l38 = t224;
                                }
                                goto ix200;
ie200:;
ix200:;
                            }
                            {
                                uint8_t t225 = 0;
                                uint8_t t226 = 0;
                                uint8_t t227 = gm_l21;
                                if (t227) {
                                    uint8_t t228;
                                    uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 0));
                                    if (t231 != ((uint8_t)0ULL)) goto mn230;
                                    t228 = ((uint8_t)1);
                                    goto me229;
mn230:;
                                    t228 = ((uint8_t)0);
                                    goto me229;
mn232:;
                                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 305, 47);
me229:;
                                    t226 = t228;
                                } else t226 = 0;
                                if (t226) {
                                    uint8_t t233 = gm_l38;
                                    t225 = t233;
                                } else t225 = 0;
                                gm_l40 = t225;
                            }
                            {
                                uint8_t t234 = 0;
                                uint8_t t235 = 0;
                                uint8_t t236 = 0;
                                uint8_t t237 = gm_l36;
                                if (t237) {
                                    uint8_t t238 = gm_l40;
                                    uint8_t t239 = (uint8_t)!t238;
                                    t236 = t239;
                                } else t236 = 0;
                                if (t236) {
                                    uint8_t t240 = gm_l19;
                                    uint8_t t241 = (uint8_t)!t240;
                                    t235 = t241;
                                } else t235 = 0;
                                if (t235) {
                                    uint8_t t242 = 0;
                                    uint8_t t243 = gm_l18;
                                    if (!t243) {
                                        uint8_t t244 = gm_l37;
                                        t242 = t244;
                                    } else t242 = 1;
                                    t234 = t242;
                                } else t234 = 0;
                                gm_l41 = t234;
                            }
                            {
                                gm_tf5srh4mp4b3i t245;
                                memset(&t245, 0, sizeof t245);
                                gm_l42 = t245;
                            }
                            {
                                uint64_t t247 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                uint8_t t248 = (t247 > ((uint64_t)0ULL));
                                if (!t248) goto ie246;
                                {
                                    uint64_t t250 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                    uint64_t r251; bool o252 = __builtin_sub_overflow(t250, ((uint64_t)1ULL), &r251);
                                    if (o252) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 311, 38);
                                    uint64_t t253 = r251;
                                    uint64_t t254 = (uintptr_t)t253;
                                    uint64_t t255 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                    if (t254 >= (uintptr_t)(t255)) gm_trap_index(t254, t255, gm_srceoep1gquqdsq, 311, 26);
                                    gm_tsnno3vacsa4v t256 = GM_LD(gm_tsnno3vacsa4v, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t254 * 48));
                                    F.l43 = t256;
                                }
                                {
                                    uint8_t t258 = GM_LD(uint8_t, (((uint8_t *)&F.l43) + 9));
                                    if (!t258) goto ie257;
                                    {
                                        if (!(((((uint8_t *)&F.l43) + 16))[16] != 0)) goto ie260;
                                        gm_t2lqu8jsj511d t261 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)&F.l43) + 16));
                                        gm_l44 = t261;
                                        {
                                            if (!((((uint8_t *)&gm_l31))[8] != 0)) goto ie263;
                                            uint64_t t264 = GM_LD(uint64_t, ((uint8_t *)&gm_l31));
                                            gm_l45 = t264;
                                            {
                                                gm_b16_8 t267 = gm_l0;
                                                uint64_t t268 = gm_l45;
                                                uint64_t t269 = (uintptr_t)t268;
                                                uint64_t t270 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                                if (t269 >= (uintptr_t)(t270)) gm_trap_index(t269, t270, gm_srceoep1gquqdsq, 315, 48);
                                                uint64_t t271 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t269 * 40) + 0));
                                                uint64_t t272 = gm_f_column_r91o38o(t267, t271);
                                                uint64_t t273 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 0));
                                                uint8_t t274 = ((t272 == t273));
                                                if (!t274) goto ie266;
                                                {
                                                    uint64_t t276 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                                                    gm_tf5srh4mp4b3i t277;
                                                    GM_ST(uint8_t, (uint8_t *)&t277 + 9, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t277 + 10, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t277 + 12, 0);
                                                    GM_ST(uint64_t, ((uint8_t *)&t277), t276);
                                                    ((uint8_t *)&t277)[8] = 1;
                                                    gm_l42 = t277;
                                                }
                                                goto ix266;
ie266:;
ix266:;
                                            }
                                            goto ix263;
ie263:;
ix263:;
                                        }
                                        goto ix260;
ie260:;
ix260:;
                                    }
                                    goto ix257;
ie257:;
ix257:;
                                }
                                goto ix246;
ie246:;
ix246:;
                            }
                            {
                                uint64_t t278 = gm_l16;
                                gm_l46 = t278;
                            }
                            {
                                uint8_t t280 = gm_l41;
                                if (!t280) goto ie279;
                                {
                                    uint64_t t282 = gm_l46;
                                    uint64_t r283; bool o284 = __builtin_add_overflow(t282, ((uint64_t)1ULL), &r283);
                                    if (o284) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 321, 32);
                                    uint64_t t285 = r283;
                                    gm_l46 = t285;
                                }
                                goto ix279;
ie279:;
ix279:;
                            }
                            {
                                uint64_t t286;
                                if (!((((uint8_t *)&gm_l42))[8] != 0)) goto ie287;
                                uint64_t t288 = GM_LD(uint64_t, ((uint8_t *)&gm_l42));
                                gm_l47 = t288;
                                uint64_t t290 = gm_l47;
                                uint64_t t291 = t290;
                                t286 = t291;
                                goto ix287;
ie287:;
                                uint64_t t293 = gm_l46;
                                uint64_t r294; bool o295 = __builtin_mul_overflow(t293, ((uint64_t)4ULL), &r294);
                                if (o295) gm_trap("integer overflow in `*`", gm_srceoep1gquqdsq, 322, 59);
                                uint64_t t296 = r294;
                                uint64_t t297 = t296;
                                t286 = t297;
ix287:;
                                gm_l32 = t286;
                            }
ix155:;
                        }
                        {
                            uint64_t t298 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                            gm_l17 = t298;
                        }
                        {
                            void * t299 = (void *)((uint8_t *)&F.l11);
                            uint64_t t300 = gm_l32;
                            gm_b0_1 t301 = gm_f_push_spaces_732bua4((void *)NULL, cx, NULL, t299, t300);
                            if (gm_unw.kind) {
                                goto gm_ll80;
                            }
                        }
                        {
                            gm_l12 = ((uint8_t)1);
                        }
                        {
                            gm_t8c5h7u09n10a t302;
                            GM_ST(uint8_t, (uint8_t *)&t302 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t302 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t302 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t302 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t302 + 0), ((uint8_t)0ULL));
                            gm_l14 = t302;
                        }
                        goto ix146;
ie146:;
                        uint8_t t304 = gm_l26;
                        if (!t304) goto ie303;
                        {
                            void * t307 = (void *)((uint8_t *)&F.l15);
                            gm_ttlblounidicj t308;
                            gm_f_pop_f3uajlg_3((void *)&t308, t307);
                            gm_ttlblounidicj t309 = t308;
                            if (!((((uint8_t *)&t309))[48] != 0)) goto ie306;
                            gm_tsnno3vacsa4v t310 = GM_LD(gm_tsnno3vacsa4v, ((uint8_t *)&t309));
                            F.l48 = t310;
                            {
                                uint8_t t312 = 0;
                                uint8_t t313 = GM_LD(uint8_t, (((uint8_t *)&F.l48) + 40));
                                if (t313) {
                                    uint8_t t314 = gm_l27;
                                    t312 = t314;
                                } else t312 = 0;
                                gm_l21 = t312;
                            }
                            {
                                uint64_t t316 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                uint64_t t317 = gm_l17;
                                uint8_t t318 = (t316 < t317);
                                if (!t318) goto ie315;
                                {
                                    uint64_t t320 = GM_LD(uint64_t, (((uint8_t *)&F.l48) + 0));
                                    gm_l16 = t320;
                                }
                                {
                                    uint64_t t321 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                    gm_l17 = t321;
                                }
                                goto ix315;
ie315:;
ix315:;
                            }
                            goto ix306;
ie306:;
ix306:;
                        }
                        goto ix303;
ie303:;
ix303:;
ix146:;
                    }
                    {
                        gm_l49 = ((uint8_t)0);
                    }
                    {
                        uint8_t t329 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 0));
                        switch (t329) { case ((uint8_t)0ULL): goto ma327; case ((uint8_t)1ULL): goto ma324; case ((uint8_t)2ULL): goto ma323; case ((uint8_t)3ULL): goto ma325; case ((uint8_t)4ULL): goto ma326; default: goto ma328; }
ma323:;
                        uint8_t t331 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 0));
                        if (t331 != ((uint8_t)2ULL)) goto mn330;
                        goto me322;
mn330:;
ma324:;
                        uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 0));
                        if (t334 != ((uint8_t)1ULL)) goto mn333;
                        {
                            void * t336 = (void *)((uint8_t *)&F.l11);
                            gm_tftb1odc739t5 t337 = gm_f_push_t83rg2m(t336, ((uint8_t)59ULL));
                            gm_tftb1odc739t5 t338 = t337;
                            if (((((uint8_t *)&t338))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t338) + 1, gm_srceoep1gquqdsq, 341, 17);
                            gm_b0_1 t339 = GM_LD(gm_b0_1, (((uint8_t *)&t338) + 1));
                        }
                        {
                            gm_t8c5h7u09n10a t340;
                            GM_ST(uint8_t, (uint8_t *)&t340 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t340 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t340 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t340 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t340 + 0), ((uint8_t)2ULL));
                            gm_l14 = t340;
                        }
                        {
                            gm_l18 = ((uint8_t)0);
                        }
                        goto me322;
mn333:;
ma325:;
                        uint8_t t342 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 0));
                        if (t342 != ((uint8_t)3ULL)) goto mn341;
                        gm_t6j6smj7qoctu t343 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&F.l25) + 8));
                        gm_l50 = t343;
                        {
                            uint8_t t346 = gm_l30;
                            uint8_t t347 = (uint8_t)!t346;
                            if (!t347) goto ie345;
                            {
                                uint64_t t350 = GM_LD(uintptr_t, (((uint8_t *)&gm_l50) + 16) + 8);
                                uint8_t t351 = (t350 > ((uint64_t)0ULL));
                                if (!t351) goto ie349;
                                gm_b16_8 t352 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 16));
                                uint8_t t353 = gm_f_only_spaces_bvta5sq(t352);
                                if (!t353) goto ie349;
                                {
                                    void * t355 = (void *)((uint8_t *)&F.l11);
                                    gm_b16_8 t356 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 16));
                                    gm_tftb1odc739t5 t357 = gm_f_extend_0cs85gs(t355, t356);
                                    gm_tftb1odc739t5 t358 = t357;
                                    if (((((uint8_t *)&t358))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t358) + 1, gm_srceoep1gquqdsq, 348, 25);
                                    gm_b0_1 t359 = GM_LD(gm_b0_1, (((uint8_t *)&t358) + 1));
                                }
                                goto ix349;
ie349:;
                                {
                                    void * t361 = (void *)((uint8_t *)&F.l11);
                                    gm_tftb1odc739t5 t362 = gm_f_push_t83rg2m(t361, ((uint8_t)32ULL));
                                    gm_tftb1odc739t5 t363 = t362;
                                    if (((((uint8_t *)&t363))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t363) + 1, gm_srceoep1gquqdsq, 350, 25);
                                    gm_b0_1 t364 = GM_LD(gm_b0_1, (((uint8_t *)&t363) + 1));
                                }
ix349:;
                            }
                            goto ix345;
ie345:;
ix345:;
                        }
                        {
                            void * t365 = (void *)((uint8_t *)&F.l11);
                            gm_b16_8 t366 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 0));
                            gm_tftb1odc739t5 t367 = gm_f_extend_0cs85gs(t365, t366);
                            gm_tftb1odc739t5 t368 = t367;
                            if (((((uint8_t *)&t368))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t368) + 1, gm_srceoep1gquqdsq, 353, 17);
                            gm_b0_1 t369 = GM_LD(gm_b0_1, (((uint8_t *)&t368) + 1));
                        }
                        {
                            gm_t8c5h7u09n10a t370;
                            GM_ST(uint8_t, (uint8_t *)&t370 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t370 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t370 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t370 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t370 + 0), ((uint8_t)2ULL));
                            gm_l14 = t370;
                        }
                        goto me322;
mn341:;
ma326:;
                        uint8_t t372 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 0));
                        if (t372 != ((uint8_t)4ULL)) goto mn371;
                        gm_b16_8 t373 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 8));
                        gm_l51 = t373;
                        {
                            gm_l52 = ((uint8_t)0);
                        }
                        {
                            uint8_t t376 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                            if (t376 != ((uint8_t)1ULL)) goto ie375;
                            uint64_t t377 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                            gm_l53 = t377;
                            {
                                uint64_t t379 = gm_l53;
                                uint64_t t380 = (uintptr_t)t379;
                                uint64_t t381 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                if (t380 >= (uintptr_t)(t381)) gm_trap_index(t380, t381, gm_srceoep1gquqdsq, 358, 48);
                                uint8_t t382 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t380 * 40) + 33));
                                gm_l52 = t382;
                            }
                            goto ix375;
ie375:;
ix375:;
                        }
                        {
                            uint8_t t384 = gm_l30;
                            uint8_t t385 = (uint8_t)!t384;
                            if (!t385) goto ie383;
                            uint8_t t386 = gm_l52;
                            uint8_t t387 = (uint8_t)!t386;
                            if (!t387) goto ie383;
                            {
                                void * t389 = (void *)((uint8_t *)&F.l11);
                                gm_tftb1odc739t5 t390 = gm_f_push_t83rg2m(t389, ((uint8_t)32ULL));
                                gm_tftb1odc739t5 t391 = t390;
                                if (((((uint8_t *)&t391))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t391) + 1, gm_srceoep1gquqdsq, 359, 48);
                                gm_b0_1 t392 = GM_LD(gm_b0_1, (((uint8_t *)&t391) + 1));
                            }
                            goto ix383;
ie383:;
ix383:;
                        }
                        {
                            void * t393 = (void *)((uint8_t *)&F.l11);
                            gm_b16_8 t394 = gm_l51;
                            gm_b16_8 t395;
                            GM_ST(void *, (uint8_t *)&t395, (void *)(gm_skmt2kt2p7v0u));
                            GM_ST(uintptr_t, (uint8_t *)&t395 + 8, (uintptr_t)(2));
                            gm_b16_8 t396;
                            GM_ST(void *, (uint8_t *)&t396, (void *)(gm_so514oj5nbhcu));
                            GM_ST(uintptr_t, (uint8_t *)&t396 + 8, (uintptr_t)(1));
                            gm_tftb1odc739t5 t397 = gm_f_replace_lf6hcmn(t393, t394, t395, t396);
                            gm_tftb1odc739t5 t398 = t397;
                            if (((((uint8_t *)&t398))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t398) + 1, gm_srceoep1gquqdsq, 361, 17);
                            gm_b0_1 t399 = GM_LD(gm_b0_1, (((uint8_t *)&t398) + 1));
                        }
                        {
                            gm_t8c5h7u09n10a t400;
                            GM_ST(uint8_t, (uint8_t *)&t400 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t400 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t400 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t400 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t400 + 0), ((uint8_t)2ULL));
                            gm_l14 = t400;
                        }
                        goto me322;
mn371:;
ma327:;
                        uint8_t t402 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 0));
                        if (t402 != ((uint8_t)0ULL)) goto mn401;
                        uint64_t t403 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 8));
                        gm_l54 = t403;
                        {
                            uint64_t t405 = gm_l54;
                            uint64_t t406 = (uintptr_t)t405;
                            uint64_t t407 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                            if (t406 >= (uintptr_t)(t407)) gm_trap_index(t406, t407, gm_srceoep1gquqdsq, 365, 25);
                            gm_t88jm1dgsmkjs t408 = GM_LD(gm_t88jm1dgsmkjs, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t406 * 40));
                            F.l55 = t408;
                        }
                        {
                            gm_l56 = ((uint64_t)1ULL);
                        }
                        {
                            gm_l57 = ((uint8_t)0);
                        }
                        {
                            uint8_t t414 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                            switch (t414) { case ((uint8_t)0ULL): goto ma410; case ((uint8_t)1ULL): goto ma411; case ((uint8_t)2ULL): goto ma412; default: goto ma413; }
ma410:;
                            uint8_t t416 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                            if (t416 != ((uint8_t)0ULL)) goto mn415;
                            goto me409;
mn415:;
ma411:;
                            uint8_t t419 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                            if (t419 != ((uint8_t)1ULL)) goto mn418;
                            uint64_t t420 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                            gm_l58 = t420;
                            {
                                uint64_t t422 = gm_l58;
                                uint64_t t423 = (uintptr_t)t422;
                                uint64_t t424 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                if (t423 >= (uintptr_t)(t424)) gm_trap_index(t423, t424, gm_srceoep1gquqdsq, 371, 34);
                                gm_t88jm1dgsmkjs t425 = GM_LD(gm_t88jm1dgsmkjs, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t423 * 40));
                                F.l59 = t425;
                            }
                            {
                                uint8_t t426 = 0;
                                uint8_t t427 = GM_LD(uint8_t, (((uint8_t *)&F.l59) + 33));
                                if (!t427) {
                                    uint8_t t428 = GM_LD(uint8_t, (((uint8_t *)&F.l55) + 32));
                                    t426 = t428;
                                } else t426 = 1;
                                gm_l60 = t426;
                            }
                            {
                                uint64_t t429 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                void * t430 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                                uint64_t t431 = GM_LD(uint64_t, (((uint8_t *)&F.l59) + 8));
                                uint64_t t432 = (uintptr_t)t431;
                                uint64_t t433 = GM_LD(uint64_t, (((uint8_t *)&F.l55) + 0));
                                uint64_t t434 = (uintptr_t)t433;
                                if (t432 > t434 || t434 > (uintptr_t)t429) gm_trap_slice(t432, t434, t429, gm_srceoep1gquqdsq, 373, 32);
                                gm_b16_8 t435;
                                GM_ST(void *, (uint8_t *)&t435, (void *)(((uint8_t *)t430 + t432 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t435 + 8, (uintptr_t)(t434 - t432));
                                gm_l61 = t435;
                            }
                            {
                                uint64_t t436;
                                gm_b16_8 t438 = GM_LD(gm_b16_8, (((uint8_t *)&F.l55) + 16));
                                gm_t6j6smj7qoctu t439;
                                gm_b16_8 t440;
                                GM_ST(void *, (uint8_t *)&t440, (void *)(gm_ss67f0gmfen1m));
                                GM_ST(uintptr_t, (uint8_t *)&t440 + 8, (uintptr_t)(2));
                                GM_ST(gm_b16_8, (uint8_t *)&t439 + 0, t440);
                                gm_b16_8 t441;
                                GM_ST(void *, (uint8_t *)&t441, (void *)(gm_sm8obe455p2d9));
                                GM_ST(uintptr_t, (uint8_t *)&t441 + 8, (uintptr_t)(2));
                                GM_ST(gm_b16_8, (uint8_t *)&t439 + 16, t441);
                                gm_t6j6smj7qoctu t442 = t439;
                                uint8_t t443 = 0;
                                gm_b16_8 t444 = t438;
                                if (gm_eq0iqiubfm7e(((uint8_t *)&t442) + 0, ((uint8_t *)&t444))) t443 = 1;
                                if (gm_eq0iqiubfm7e(((uint8_t *)&t442) + 16, ((uint8_t *)&t444))) t443 = 1;
                                uint8_t t445 = (uint8_t)(t443);
                                if (!t445) goto ie437;
                                uint64_t t447 = ((uint64_t)1ULL);
                                t436 = t447;
                                goto ix437;
ie437:;
                                uint64_t t449 = ((uint64_t)2ULL);
                                t436 = t449;
ix437:;
                                gm_l62 = t436;
                            }
                            {
                                uint8_t t451 = gm_l60;
                                uint8_t t452 = (uint8_t)!t451;
                                if (!t452) goto ie450;
                                uint64_t t453 = GM_LD(uintptr_t, ((uint8_t *)&gm_l61) + 8);
                                uint64_t t454 = gm_l62;
                                uint8_t t455 = (t453 >= t454);
                                if (!t455) goto ie450;
                                gm_b16_8 t456 = gm_l61;
                                uint8_t t457 = gm_f_only_spaces_bvta5sq(t456);
                                if (!t457) goto ie450;
                                gm_b16_8 t458 = gm_l0;
                                uint8_t t459 = gm_f_keeps_padding_m0v49h1(t458, &(*(gm_t88jm1dgsmkjs *)((uint8_t *)&F.l59)), &(*(gm_t88jm1dgsmkjs *)((uint8_t *)&F.l55)));
                                if (!t459) goto ie450;
                                {
                                    gm_b16_8 t461 = gm_l0;
                                    gm_b16_8 t462 = t461;
                                    uint64_t t463 = GM_LD(uintptr_t, ((uint8_t *)&t462) + 8);
                                    void * t464 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t462)));
                                    gm_b16_8 t465 = gm_l0;
                                    uint64_t t466 = GM_LD(uint64_t, (((uint8_t *)&F.l55) + 0));
                                    uint64_t t467 = gm_f_line_start_pl10jt5(t465, t466);
                                    uint64_t t468 = (uintptr_t)t467;
                                    uint64_t t469 = GM_LD(uint64_t, (((uint8_t *)&F.l55) + 0));
                                    uint64_t t470 = (uintptr_t)t469;
                                    if (t468 > t470 || t470 > (uintptr_t)t463) gm_trap_slice(t468, t470, t463, gm_srceoep1gquqdsq, 381, 53);
                                    gm_b16_8 t471;
                                    GM_ST(void *, (uint8_t *)&t471, (void *)(((uint8_t *)t464 + t468 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t471 + 8, (uintptr_t)(t470 - t468));
                                    uint64_t t472 = gm_f_relative_column_ctuu0pd(t471);
                                    gm_l63 = t472;
                                }
                                {
                                    gm_b16_8 t473 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                                    gm_b16_8 t474 = t473;
                                    gm_b16_8 t475 = GM_LD(gm_b16_8, ((uint8_t *)&t474));
                                    gm_b16_8 t476 = gm_f_last_line_hk6q5l4(t475);
                                    uint64_t t477 = gm_f_relative_column_ctuu0pd(t476);
                                    gm_l64 = t477;
                                }
                                {
                                    uint64_t t478;
                                    uint64_t t480 = gm_l63;
                                    uint64_t t481 = gm_l64;
                                    uint8_t t482 = (t480 > t481);
                                    if (!t482) goto ie479;
                                    uint64_t t484 = gm_l63;
                                    uint64_t t485 = gm_l64;
                                    uint64_t r486; bool o487 = __builtin_sub_overflow(t484, t485, &r486);
                                    if (o487) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 383, 52);
                                    uint64_t t488 = r486;
                                    uint64_t t489 = t488;
                                    t478 = t489;
                                    goto ix479;
ie479:;
                                    uint64_t t491 = ((uint64_t)1ULL);
                                    t478 = t491;
ix479:;
                                    gm_l56 = t478;
                                }
                                goto ix450;
ie450:;
ix450:;
                            }
                            {
                                uint8_t t492 = 0;
                                uint8_t t493 = 0;
                                uint64_t t494 = GM_LD(uintptr_t, ((uint8_t *)&gm_l61) + 8);
                                uint8_t t495 = ((t494 == ((uint64_t)0ULL)));
                                if (t495) {
                                    uint64_t t496 = GM_LD(uintptr_t, (((uint8_t *)&F.l59) + 16) + 8);
                                    uint64_t r497; bool o498 = __builtin_sub_overflow(t496, ((uint64_t)1ULL), &r497);
                                    if (o498) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 386, 70);
                                    uint64_t t499 = r497;
                                    uint64_t t500 = (uintptr_t)t499;
                                    uint64_t t501 = GM_LD(uintptr_t, (((uint8_t *)&F.l59) + 16) + 8);
                                    if (t500 >= (uintptr_t)(t501)) gm_trap_index(t500, t501, gm_srceoep1gquqdsq, 386, 59);
                                    uint8_t t502 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l59) + 16)) + t500 * 1));
                                    uint8_t t503 = gm_f_is_word_kb7cdmu(t502);
                                    t493 = t503;
                                } else t493 = 0;
                                if (t493) {
                                    uint64_t t504 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t505 = GM_LD(uintptr_t, (((uint8_t *)&F.l55) + 16) + 8);
                                    if (t504 >= (uintptr_t)(t505)) gm_trap_index(t504, t505, gm_srceoep1gquqdsq, 386, 103);
                                    uint8_t t506 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l55) + 16)) + t504 * 1));
                                    uint8_t t507 = gm_f_is_word_kb7cdmu(t506);
                                    t492 = t507;
                                } else t492 = 0;
                                gm_l65 = t492;
                            }
                            {
                                uint8_t t508 = 0;
                                uint8_t t509 = gm_l65;
                                uint8_t t510 = (uint8_t)!t509;
                                if (t510) {
                                    uint8_t t511 = 0;
                                    uint8_t t512 = 0;
                                    uint8_t t513 = gm_l60;
                                    uint8_t t514 = (uint8_t)!t513;
                                    if (!t514) {
                                        gm_b16_8 t515 = GM_LD(gm_b16_8, (((uint8_t *)&F.l59) + 16));
                                        gm_b16_8 t516 = GM_LD(gm_b16_8, (((uint8_t *)&F.l55) + 16));
                                        uint8_t t517 = gm_f_would_merge_44g1m96((void *)NULL, cx, NULL, t515, t516);
                                        if (gm_unw.kind) {
                                            goto gm_ll80;
                                        }
                                        t512 = t517;
                                    } else t512 = 1;
                                    if (!t512) {
                                        gm_b16_8 t518 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                                        gm_b16_8 t519 = t518;
                                        gm_b16_8 t520 = GM_LD(gm_b16_8, ((uint8_t *)&t519));
                                        gm_b16_8 t521 = GM_LD(gm_b16_8, (((uint8_t *)&F.l55) + 16));
                                        uint8_t t522 = gm_f_merges_with_tail_ijmh3nm((void *)NULL, cx, NULL, t520, t521);
                                        if (gm_unw.kind) {
                                            goto gm_ll80;
                                        }
                                        t511 = t522;
                                    } else t511 = 1;
                                    t508 = t511;
                                } else t508 = 0;
                                gm_l57 = t508;
                            }
                            goto me409;
mn418:;
ma412:;
                            uint8_t t524 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                            if (t524 != ((uint8_t)2ULL)) goto mn523;
                            {
                                uint8_t t526 = GM_LD(uint8_t, (((uint8_t *)&F.l55) + 32));
                                uint8_t t527 = (uint8_t)!t526;
                                gm_l57 = t527;
                            }
                            goto me409;
mn523:;
ma413:;
                            gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 368, 17);
me409:;
                        }
                        {
                            uint8_t t529 = gm_l57;
                            if (!t529) goto ie528;
                            {
                                void * t531 = (void *)((uint8_t *)&F.l11);
                                uint64_t t532 = gm_l56;
                                gm_b0_1 t533 = gm_f_push_spaces_732bua4((void *)NULL, cx, NULL, t531, t532);
                                if (gm_unw.kind) {
                                    goto gm_ll80;
                                }
                            }
                            goto ix528;
ie528:;
ix528:;
                        }
                        {
                            uint8_t t535 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                            if (t535 != ((uint8_t)1ULL)) goto ie534;
                            uint64_t t536 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                            gm_l66 = t536;
                            {
                                uint64_t t539 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                uint8_t t540 = (t539 > ((uint64_t)0ULL));
                                if (!t540) goto ie538;
                                {
                                    uint64_t t542 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                    uint64_t r543; bool o544 = __builtin_sub_overflow(t542, ((uint64_t)1ULL), &r543);
                                    if (o544) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 394, 43);
                                    uint64_t t545 = r543;
                                    uint64_t t546 = (uintptr_t)t545;
                                    uint64_t t547 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                    if (t546 >= (uintptr_t)(t547)) gm_trap_index(t546, t547, gm_srceoep1gquqdsq, 394, 31);
                                    void * t548 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t546 * 48);
                                    gm_l67 = t548;
                                }
                                {
                                    void * t550 = gm_l67;
                                    uint8_t t551 = GM_LD(uint8_t, (((uint8_t *)t550) + 9));
                                    if (!t551) goto ie549;
                                    void * t552 = gm_l67;
                                    uint8_t t553;
                                    if ((((((uint8_t *)t552) + 16))[16] != 0)) goto mn555;
                                    t553 = ((uint8_t)1);
                                    goto me554;
mn555:;
                                    t553 = ((uint8_t)0);
                                    goto me554;
mn556:;
                                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 395, 40);
me554:;
                                    if (!t553) goto ie549;
                                    uint64_t t557 = gm_l66;
                                    uint64_t t558 = (uintptr_t)t557;
                                    uint64_t t559 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                    if (t558 >= (uintptr_t)(t559)) gm_trap_index(t558, t559, gm_srceoep1gquqdsq, 395, 61);
                                    uint8_t t560 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t558 * 40) + 34));
                                    if (!t560) goto ie549;
                                    uint8_t t561 = GM_LD(uint8_t, (((uint8_t *)&F.l55) + 35));
                                    uint8_t t562 = (uint8_t)!t561;
                                    if (!t562) goto ie549;
                                    {
                                        void * t564 = gm_l67;
                                        uint8_t *t565 = (((uint8_t *)t564) + 16);
                                        gm_b16_8 t566 = gm_l0;
                                        uint64_t t567 = GM_LD(uint64_t, (((uint8_t *)&F.l55) + 0));
                                        uint64_t t568 = gm_f_column_r91o38o(t566, t567);
                                        void * t569 = (void *)((uint8_t *)&F.l11);
                                        uint64_t t570 = gm_f_out_column_hr4hgng(t569);
                                        gm_t2lqu8jsj511d t571;
                                        GM_ST(uint64_t, ((uint8_t *)&t571 + 0), t568);
                                        GM_ST(uint64_t, ((uint8_t *)&t571 + 8), t570);
                                        gm_tc70a9dbbmgkg t572;
                                        GM_ST(uint8_t, (uint8_t *)&t572 + 17, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t572 + 18, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t572 + 20, 0);
                                        GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t572), t571);
                                        ((uint8_t *)&t572)[16] = 1;
                                        GM_ST(gm_tc70a9dbbmgkg, t565, t572);
                                    }
                                    goto ix549;
ie549:;
ix549:;
                                }
                                goto ix538;
ie538:;
ix538:;
                            }
                            goto ix534;
ie534:;
ix534:;
                        }
                        {
                            void * t573 = (void *)((uint8_t *)&F.l11);
                            gm_b16_8 t574 = GM_LD(gm_b16_8, (((uint8_t *)&F.l55) + 16));
                            gm_tftb1odc739t5 t575 = gm_f_extend_0cs85gs(t573, t574);
                            gm_tftb1odc739t5 t576 = t575;
                            if (((((uint8_t *)&t576))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t576) + 1, gm_srceoep1gquqdsq, 400, 17);
                            gm_b0_1 t577 = GM_LD(gm_b0_1, (((uint8_t *)&t576) + 1));
                        }
                        {
                            uint8_t t579 = GM_LD(uint8_t, (((uint8_t *)&F.l55) + 34));
                            if (!t579) goto ie578;
                            {
                                uint8_t t581 = (((GM_LD(uintptr_t, (((uint8_t *)&F.l55) + 16) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&F.l55) + 16)), gm_sh40na9qbga3l, 1) == 0));
                                gm_l68 = t581;
                            }
                            {
                                gm_l69 = ((uint8_t)0);
                            }
                            {
                                uint8_t t583 = gm_l68;
                                if (!t583) goto ie582;
                                {
                                    if (!((((uint8_t *)&gm_l20))[8] != 0)) goto ie585;
                                    uint64_t t586 = GM_LD(uint64_t, ((uint8_t *)&gm_l20));
                                    gm_l70 = t586;
                                    {
                                        uint64_t t588 = gm_l70;
                                        uint64_t t589 = (uintptr_t)t588;
                                        uint64_t t590 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                        if (t589 >= (uintptr_t)(t590)) gm_trap_index(t589, t590, gm_srceoep1gquqdsq, 405, 53);
                                        gm_b16_8 t591 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t589 * 40) + 16));
                                        gm_tfeab279c16rr t592;
                                        gm_b16_8 t593;
                                        GM_ST(void *, (uint8_t *)&t593, (void *)(gm_sl9nvct67or45));
                                        GM_ST(uintptr_t, (uint8_t *)&t593 + 8, (uintptr_t)(2));
                                        GM_ST(gm_b16_8, (uint8_t *)&t592 + 0, t593);
                                        gm_b16_8 t594;
                                        GM_ST(void *, (uint8_t *)&t594, (void *)(gm_slpo22sjs83b1));
                                        GM_ST(uintptr_t, (uint8_t *)&t594 + 8, (uintptr_t)(4));
                                        GM_ST(gm_b16_8, (uint8_t *)&t592 + 16, t594);
                                        gm_b16_8 t595;
                                        GM_ST(void *, (uint8_t *)&t595, (void *)(gm_s9es2tos1bhfp));
                                        GM_ST(uintptr_t, (uint8_t *)&t595 + 8, (uintptr_t)(4));
                                        GM_ST(gm_b16_8, (uint8_t *)&t592 + 32, t595);
                                        gm_tfeab279c16rr t596 = t592;
                                        uint8_t t597 = 0;
                                        gm_b16_8 t598 = t591;
                                        if (gm_eq0iqiubfm7e(((uint8_t *)&t596) + 0, ((uint8_t *)&t598))) t597 = 1;
                                        if (gm_eq0iqiubfm7e(((uint8_t *)&t596) + 16, ((uint8_t *)&t598))) t597 = 1;
                                        if (gm_eq0iqiubfm7e(((uint8_t *)&t596) + 32, ((uint8_t *)&t598))) t597 = 1;
                                        uint8_t t599 = (uint8_t)(t597);
                                        gm_l69 = t599;
                                    }
                                    goto ix585;
ie585:;
ix585:;
                                }
                                goto ix582;
ie582:;
ix582:;
                            }
                            {
                                void * t600 = (void *)((uint8_t *)&F.l15);
                                gm_tsnno3vacsa4v t601;
                                memset((uint8_t *)&t601 + 10, 0, 6);
                                memset((uint8_t *)&t601 + 41, 0, 7);
                                uint64_t t602 = gm_l16;
                                GM_ST(uint64_t, (uint8_t *)&t601 + 0, t602);
                                uint8_t t603 = 0;
                                uint8_t t604 = gm_l68;
                                if (t604) {
                                    uint8_t t605 = GM_LD(uint8_t, (((uint8_t *)&F.l55) + 36));
                                    uint8_t t606 = (uint8_t)!t605;
                                    t603 = t606;
                                } else t603 = 0;
                                GM_ST(uint8_t, (uint8_t *)&t601 + 8, t603);
                                uint8_t t607 = gm_l68;
                                uint8_t t608 = (uint8_t)!t607;
                                GM_ST(uint8_t, (uint8_t *)&t601 + 9, t608);
                                gm_tc70a9dbbmgkg t609;
                                memset(&t609, 0, sizeof t609);
                                GM_ST(gm_tc70a9dbbmgkg, (uint8_t *)&t601 + 16, t609);
                                uint8_t t610 = gm_l69;
                                GM_ST(uint8_t, (uint8_t *)&t601 + 40, t610);
                                gm_tftb1odc739t5 t611 = gm_f_push_8h1r7s6_3(t600, &t601);
                                gm_tftb1odc739t5 t612 = t611;
                                if (((((uint8_t *)&t612))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t612) + 1, gm_srceoep1gquqdsq, 407, 21);
                                gm_b0_1 t613 = GM_LD(gm_b0_1, (((uint8_t *)&t612) + 1));
                            }
                            goto ix578;
ie578:;
ix578:;
                        }
                        {
                            uint8_t t614 = gm_f_continues_line_2cliit4(&(*(gm_t88jm1dgsmkjs *)((uint8_t *)&F.l55)));
                            gm_l18 = t614;
                        }
                        {
                            uint8_t t616 = GM_LD(uint8_t, (((uint8_t *)&F.l55) + 35));
                            uint8_t t617 = (uint8_t)!t616;
                            if (!t617) goto ie615;
                            {
                                gm_l21 = ((uint8_t)0);
                            }
                            goto ix615;
ie615:;
ix615:;
                        }
                        {
                            uint8_t t619 = 0;
                            uint8_t t620 = (((GM_LD(uintptr_t, (((uint8_t *)&F.l55) + 16) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&F.l55) + 16)), gm_sh40na9qbga3l, 1) == 0));
                            if (t620) {
                                uint8_t t621 = GM_LD(uint8_t, (((uint8_t *)&F.l55) + 33));
                                uint8_t t622 = (uint8_t)!t621;
                                t619 = t622;
                            } else t619 = 0;
                            gm_l49 = t619;
                        }
                        {
                            uint64_t t623 = gm_l54;
                            gm_t8c5h7u09n10a t624;
                            GM_ST(uint8_t, (uint8_t *)&t624 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t624 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t624 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t624 + 0), ((uint8_t)1ULL));
                            GM_ST(uint64_t, ((uint8_t *)&t624 + 8), t623);
                            gm_l14 = t624;
                        }
                        goto me322;
mn401:;
ma328:;
                        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 338, 9);
me322:;
                    }
                    {
                        uint8_t t625 = 0;
                        uint8_t t626 = gm_l49;
                        if (!t626) {
                            uint8_t t627 = 0;
                            uint8_t t628 = gm_l29;
                            if (t628) {
                                uint8_t t629 = gm_l19;
                                t627 = t629;
                            } else t627 = 0;
                            t625 = t627;
                        } else t625 = 1;
                        gm_l19 = t625;
                    }
                    goto me84;
mn85:;
                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 262, 5);
me84:;
                }
lc76:;
                {
                    uint64_t t631 = gm_l23;
                    uint64_t r632; bool o633 = __builtin_add_overflow(t631, ((uint64_t)1ULL), &r632);
                    uint64_t t634 = r632;
                    gm_l23 = t634;
                }
            }
gm_ll80:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb76; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc76; }
            }
            goto gm_ld1_3;
lb76:;
        }
    }
    {
        uint8_t t636 = gm_l12;
        if (!t636) goto ie635;
        {
            void * t638 = (void *)((uint8_t *)&F.l11);
            gm_tftb1odc739t5 t639 = gm_f_push_t83rg2m(t638, ((uint8_t)10ULL));
            gm_tftb1odc739t5 t640 = t639;
            if (((((uint8_t *)&t640))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t640) + 1, gm_srceoep1gquqdsq, 423, 20);
            gm_b0_1 t641 = GM_LD(gm_b0_1, (((uint8_t *)&t640) + 1));
        }
        goto ix635;
ie635:;
ix635:;
    }
    {
        uint8_t t642 = 0;
        F.ret = (*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l11));
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t642;
        goto gm_ld1_3;
    }
    {
        void * t643 = (void *)((uint8_t *)&F.l15);
        gm_f_deinit_ectdopm_2(t643);
    }
    {
        void * t644 = (void *)((uint8_t *)&F.l5);
        gm_f_deinit_aii0n0l(t644);
    }
    {
        void * t645 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_26uqnl1(t645);
    }
    goto gm_ls646;
gm_ld1_3:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t647 = (void *)((uint8_t *)&F.l15);
        gm_f_deinit_ectdopm_2(t647);
        gm_unw = gm_saved;
    }
gm_ld1_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t648 = (void *)((uint8_t *)&F.l5);
        gm_f_deinit_aii0n0l(t648);
        gm_unw = gm_saved;
    }
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t649 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_26uqnl1(t649);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls646:;
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
__attribute__((aligned(4))) uint8_t gm_f_merges_with_tail_ijmh3nm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_merges_with_tail_ijmh3nm F;
    gm_b16_8 gm_l0; /* out */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* tail */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 641u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) {
            uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t6 >= (uintptr_t)(t7)) gm_trap_index(t6, t7, gm_srceoep1gquqdsq, 107, 40);
            uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t6 * 1));
            uint8_t t9 = gm_f_is_punct_char_3bc3hom(t8);
            uint8_t t10 = (uint8_t)!t9;
            t3 = t10;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t12 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l2 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l2;
            uint8_t t16 = (t15 > ((uint64_t)0ULL));
            if (!t16) goto lb14;
            uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint64_t t18 = gm_l2;
            uint64_t r19; bool o20 = __builtin_sub_overflow(t17, t18, &r19);
            if (o20) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 109, 25);
            uint64_t t21 = r19;
            uint8_t t22 = (t21 < ((uint64_t)3ULL));
            if (!t22) goto lb14;
            uint64_t t23 = gm_l2;
            uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
            if (o25) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 109, 67);
            uint64_t t26 = r24;
            uint64_t t27 = (uintptr_t)t26;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srceoep1gquqdsq, 109, 63);
            uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t27 * 1));
            uint8_t t30 = gm_f_is_punct_char_3bc3hom(t29);
            if (!t30) goto lb14;
            {
                uint64_t t33 = gm_l2;
                uint64_t r34; bool o35 = __builtin_sub_overflow(t33, ((uint64_t)1ULL), &r34);
                if (o35) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 109, 81);
                uint64_t t36 = r34;
                gm_l2 = t36;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t38 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t39 = gm_l2;
        uint64_t t40 = (uintptr_t)t39;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t42 = (uintptr_t)t41;
        if (t40 > t42 || t42 > (uintptr_t)t37) gm_trap_slice(t40, t42, t37, gm_srceoep1gquqdsq, 110, 13);
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t38 + t40 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(t42 - t40));
        gm_l3 = t43;
    }
    {
        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t46 = (t45 < ((uint64_t)2ULL));
        if (!t46) goto ie44;
        {
            uint8_t t48 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        gm_f_new_jr5ejs0((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t50 = (void *)((uint8_t *)&F.l4);
        gm_t3mf58gv9kf3n t51 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t50));
        gm_t3mf58gv9kf3n t52 = t51;
        gm_node wn53 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t52, .hctx = cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
        {
            gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, (&wn53), NULL);
            if (gm_unw.kind) {
                goto gm_ll55;
            }
        }
        {
            void * t57 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t58 = gm_l3;
            gm_tftb1odc739t5 t59 = gm_f_extend_0cs85gs(t57, t58);
            gm_tftb1odc739t5 t60 = t59;
            if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_srceoep1gquqdsq, 116, 9);
            gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
        }
        {
            void * t62 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t63 = gm_l1;
            gm_tftb1odc739t5 t64 = gm_f_extend_0cs85gs(t62, t63);
            gm_tftb1odc739t5 t65 = t64;
            if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_srceoep1gquqdsq, 117, 9);
            gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
        }
        {
            gm_f_diagnostics_n3kkhf4((void *)&F.l6, (void *)NULL, (&wn53), NULL);
            if (gm_unw.kind) {
                goto gm_ll55;
            }
        }
        {
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t68 = t67;
            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
            void * t70 = (void *)((uint8_t *)&F.l6);
            gm_f_lex_dc8mbmf((void *)&F.l7, (void *)NULL, (&wn53), NULL, ((uint32_t)0ULL), t69, t70);
            if (gm_unw.kind) {
                goto gm_ll55;
            }
        }
        {
            {
                gm_b16_8 t72 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l7) + 0) + 0));
                gm_l8 = t72;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t74 = gm_l9;
                    uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t76 = (t74 < t75);
                    if (!t76) goto lb73;
                    {
                        uint64_t t79 = gm_l9;
                        uint64_t t80 = (uintptr_t)t79;
                        uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        gm_tm4pc0emhrv1g t84 = GM_LD(gm_tm4pc0emhrv1g, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t80 * 48));
                        F.l10 = t84;
                        {
                            uint32_t t87 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 32) + 4));
                            uint64_t t88 = (uint64_t)t87;
                            uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            uint8_t t90 = ((t88 == t89));
                            if (!t90) goto ie86;
                            {
                                uint8_t t92 = 0;
                                F.ret = ((uint8_t)0);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t92;
                                goto gm_ll77;
                            }
                            goto ix86;
ie86:;
ix86:;
                        }
                        {
                            uint32_t t94 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 32) + 4));
                            uint64_t t95 = (uint64_t)t94;
                            uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            uint8_t t97 = (t95 < t96);
                            if (!t97) goto ie93;
                            uint32_t t98 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 32) + 8));
                            uint64_t t99 = (uint64_t)t98;
                            uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            uint8_t t101 = (t99 > t100);
                            if (!t101) goto ie93;
                            {
                                uint8_t t103 = 0;
                                F.ret = ((uint8_t)1);
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t103;
                                goto gm_ll77;
                            }
                            goto ix93;
ie93:;
ix93:;
                        }
                        goto me82;
mn83:;
                        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 120, 9);
me82:;
                    }
lc73:;
                    {
                        uint64_t t105 = gm_l9;
                        uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                        uint64_t t108 = r106;
                        gm_l9 = t108;
                    }
                }
gm_ll77:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb73; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc73; }
                }
                goto gm_ll55;
lb73:;
            }
        }
        {
            uint8_t t109 = 0;
            F.ret = ((uint8_t)1);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t109;
            goto gm_ll55;
        }
        goto gm_ls110;
gm_ll55:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we54; }
        goto gm_ld1_1;
gm_ls110:;
we54:;
    }
    {
        void * t111 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_sejdpqv(t111);
    }
    goto gm_ls112;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t113 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_sejdpqv(t113);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls112:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_tok_qap88op(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_tok_qap88op F;
    gm_b16_8 gm_l0; /* items */
    uint64_t gm_l1; /* from */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* k */
    uint64_t gm_l5; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 236u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            uint64_t t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
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
                        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_srceoep1gquqdsq, 218, 23);
                        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t19 * 40) + 0));
                        if (t21 != ((uint8_t)0ULL)) goto ie17;
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t19 * 40) + 8));
                        gm_l5 = t22;
                        {
                            uint64_t t24 = gm_l5;
                            gm_tf5srh4mp4b3i t25;
                            GM_ST(uint8_t, (uint8_t *)&t25 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t25 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t25 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t25), t24);
                            ((uint8_t *)&t25)[8] = 1;
                            uint8_t t26 = 0;
                            F.ret = t25;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 217, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t28 = gm_l3;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l3 = t31;
                }
            }
lb5:;
        }
    }
    {
        gm_tf5srh4mp4b3i t32;
        memset(&t32, 0, sizeof t32);
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_relative_column_ctuu0pd(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_gap_tkq0f9l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_scan_gap_tkq0f9l F;
    gm_b16_8 gm_l0; /* gap */
    void * gm_l1; /* items */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* ws_start */
    uint8_t gm_l4; /* b */
    uint64_t gm_l5; /* end */
    int32_t gm_l6; /* depth */
    uint64_t gm_l7; /* j */
    GM_STACK_CHECK(&F, 1185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        gm_l3 = ((uint64_t)0ULL);
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
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srceoep1gquqdsq, 31, 14);
                uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
                gm_l4 = t11;
            }
            {
                uint8_t t13 = gm_l4;
                uint8_t t14 = ((t13 == ((uint8_t)10ULL)));
                if (!t14) goto ie12;
                {
                    void * t16 = gm_l1;
                    gm_tj63kbqqj2092 t17;
                    memset(&t17, 0, sizeof t17);
                    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)2ULL));
                    gm_tftb1odc739t5 t18 = gm_f_push_1e500nc_4(t16, &t17);
                    gm_tftb1odc739t5 t19 = t18;
                    if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 1, gm_srceoep1gquqdsq, 33, 13);
                    gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 1));
                }
                {
                    uint64_t t21 = gm_l2;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    if (o23) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 34, 13);
                    uint64_t t24 = r22;
                    gm_l2 = t24;
                }
                {
                    uint64_t t25 = gm_l2;
                    gm_l3 = t25;
                }
                goto ix12;
ie12:;
                uint8_t t27 = gm_l4;
                uint8_t t28 = ((t27 == ((uint8_t)59ULL)));
                if (!t28) goto ie26;
                {
                    void * t30 = gm_l1;
                    gm_tj63kbqqj2092 t31;
                    memset(&t31, 0, sizeof t31);
                    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)1ULL));
                    gm_tftb1odc739t5 t32 = gm_f_push_1e500nc_4(t30, &t31);
                    gm_tftb1odc739t5 t33 = t32;
                    if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srceoep1gquqdsq, 37, 13);
                    gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
                }
                {
                    uint64_t t35 = gm_l2;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    if (o37) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 38, 13);
                    uint64_t t38 = r36;
                    gm_l2 = t38;
                }
                {
                    uint64_t t39 = gm_l2;
                    gm_l3 = t39;
                }
                goto ix26;
ie26:;
                uint8_t t41 = gm_l4;
                uint8_t t42 = ((t41 == ((uint8_t)47ULL)));
                if (!t42) goto ie40;
                gm_b16_8 t43 = gm_l0;
                gm_b16_8 t44 = t43;
                void * t45 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t44)));
                uint64_t t46 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t44) + 8);
                uint64_t t47 = gm_l2;
                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                if (o49) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 40, 40);
                uint64_t t50 = r48;
                uint64_t t51 = (uintptr_t)t50;
                gm_tqs5p4n2tq19v t52;
                if (t51 < t46) {
                uint8_t t53 = GM_LD(uint8_t, ((uint8_t *)t45 + t51 * 1));
                gm_tqs5p4n2tq19v t54;
                GM_ST(uint8_t, ((uint8_t *)&t54), t53);
                ((uint8_t *)&t54)[1] = 1;
                    t52 = t54;
                } else {
                gm_tqs5p4n2tq19v t55;
                memset(&t55, 0, sizeof t55);
                    t52 = t55;
                }
                gm_tqs5p4n2tq19v t56;
                GM_ST(uint8_t, ((uint8_t *)&t56), ((uint8_t)47ULL));
                ((uint8_t *)&t56)[1] = 1;
                gm_tqs5p4n2tq19v t57 = t52;
                gm_tqs5p4n2tq19v t58 = t56;
                uint8_t t59 = (gm_eqb44cb3cf1n(((uint8_t *)&t57), ((uint8_t *)&t58)));
                if (!t59) goto ie40;
                {
                    gm_b16_8 t61 = gm_l0;
                    uint64_t t62 = gm_l2;
                    uint64_t t63 = gm_f_line_end_jm825k5(t61, t62);
                    gm_l5 = t63;
                }
                {
                    void * t64 = gm_l1;
                    gm_t6j6smj7qoctu t65;
                    uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t67 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t68 = gm_l2;
                    uint64_t t69 = (uintptr_t)t68;
                    uint64_t t70 = gm_l5;
                    uint64_t t71 = (uintptr_t)t70;
                    if (t69 > t71 || t71 > (uintptr_t)t66) gm_trap_slice(t69, t71, t66, gm_srceoep1gquqdsq, 42, 54);
                    gm_b16_8 t72;
                    GM_ST(void *, (uint8_t *)&t72, (void *)(((uint8_t *)t67 + t69 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(t71 - t69));
                    gm_b16_8 t73 = gm_f_trim_end_d3d35bq(t72);
                    GM_ST(gm_b16_8, (uint8_t *)&t65 + 0, t73);
                    uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t75 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t76 = gm_l3;
                    uint64_t t77 = (uintptr_t)t76;
                    uint64_t t78 = gm_l2;
                    uint64_t t79 = (uintptr_t)t78;
                    if (t77 > t79 || t79 > (uintptr_t)t74) gm_trap_slice(t77, t79, t74, gm_srceoep1gquqdsq, 42, 72);
                    gm_b16_8 t80;
                    GM_ST(void *, (uint8_t *)&t80, (void *)(((uint8_t *)t75 + t77 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(t79 - t77));
                    GM_ST(gm_b16_8, (uint8_t *)&t65 + 16, t80);
                    gm_tj63kbqqj2092 t81;
                    GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)3ULL));
                    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t81 + 8), t65);
                    gm_tftb1odc739t5 t82 = gm_f_push_1e500nc_4(t64, &t81);
                    gm_tftb1odc739t5 t83 = t82;
                    if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_srceoep1gquqdsq, 42, 13);
                    gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
                }
                {
                    uint64_t t85 = gm_l5;
                    gm_l2 = t85;
                }
                {
                    uint64_t t86 = gm_l2;
                    gm_l3 = t86;
                }
                goto ix40;
ie40:;
                uint8_t t88 = gm_l4;
                uint8_t t89 = ((t88 == ((uint8_t)47ULL)));
                if (!t89) goto ie87;
                gm_b16_8 t90 = gm_l0;
                gm_b16_8 t91 = t90;
                void * t92 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t91)));
                uint64_t t93 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t91) + 8);
                uint64_t t94 = gm_l2;
                uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                if (o96) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 45, 40);
                uint64_t t97 = r95;
                uint64_t t98 = (uintptr_t)t97;
                gm_tqs5p4n2tq19v t99;
                if (t98 < t93) {
                uint8_t t100 = GM_LD(uint8_t, ((uint8_t *)t92 + t98 * 1));
                gm_tqs5p4n2tq19v t101;
                GM_ST(uint8_t, ((uint8_t *)&t101), t100);
                ((uint8_t *)&t101)[1] = 1;
                    t99 = t101;
                } else {
                gm_tqs5p4n2tq19v t102;
                memset(&t102, 0, sizeof t102);
                    t99 = t102;
                }
                gm_tqs5p4n2tq19v t103;
                GM_ST(uint8_t, ((uint8_t *)&t103), ((uint8_t)42ULL));
                ((uint8_t *)&t103)[1] = 1;
                gm_tqs5p4n2tq19v t104 = t99;
                gm_tqs5p4n2tq19v t105 = t103;
                uint8_t t106 = (gm_eqb44cb3cf1n(((uint8_t *)&t104), ((uint8_t *)&t105)));
                if (!t106) goto ie87;
                {
                    gm_l6 = ((int32_t)1LL);
                }
                {
                    uint64_t t108 = gm_l2;
                    uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)2ULL), &r109);
                    if (o110) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 47, 18);
                    uint64_t t111 = r109;
                    gm_l7 = t111;
                }
                {
                    for (;;) {
                        int32_t t113 = gm_l6;
                        uint8_t t114 = (t113 > ((int32_t)0LL));
                        if (!t114) goto lb112;
                        uint64_t t115 = gm_l7;
                        uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t117 = (t115 < t116);
                        if (!t117) goto lb112;
                        {
                            uint64_t t121 = gm_l7;
                            uint64_t t122 = (uintptr_t)t121;
                            uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t122 >= (uintptr_t)(t123)) gm_trap_index(t122, t123, gm_srceoep1gquqdsq, 49, 20);
                            uint8_t t124 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t122 * 1));
                            uint8_t t125 = ((t124 == ((uint8_t)47ULL)));
                            if (!t125) goto ie120;
                            gm_b16_8 t126 = gm_l0;
                            gm_b16_8 t127 = t126;
                            void * t128 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t127)));
                            uint64_t t129 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t127) + 8);
                            uint64_t t130 = gm_l7;
                            uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
                            if (o132) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 49, 46);
                            uint64_t t133 = r131;
                            uint64_t t134 = (uintptr_t)t133;
                            gm_tqs5p4n2tq19v t135;
                            if (t134 < t129) {
                            uint8_t t136 = GM_LD(uint8_t, ((uint8_t *)t128 + t134 * 1));
                            gm_tqs5p4n2tq19v t137;
                            GM_ST(uint8_t, ((uint8_t *)&t137), t136);
                            ((uint8_t *)&t137)[1] = 1;
                                t135 = t137;
                            } else {
                            gm_tqs5p4n2tq19v t138;
                            memset(&t138, 0, sizeof t138);
                                t135 = t138;
                            }
                            gm_tqs5p4n2tq19v t139;
                            GM_ST(uint8_t, ((uint8_t *)&t139), ((uint8_t)42ULL));
                            ((uint8_t *)&t139)[1] = 1;
                            gm_tqs5p4n2tq19v t140 = t135;
                            gm_tqs5p4n2tq19v t141 = t139;
                            uint8_t t142 = (gm_eqb44cb3cf1n(((uint8_t *)&t140), ((uint8_t *)&t141)));
                            if (!t142) goto ie120;
                            {
                                int32_t t144 = gm_l6;
                                int32_t r145; bool o146 = __builtin_add_overflow(t144, ((int32_t)1LL), &r145);
                                if (o146) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 50, 21);
                                int32_t t147 = r145;
                                gm_l6 = t147;
                            }
                            {
                                uint64_t t148 = gm_l7;
                                uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)2ULL), &r149);
                                if (o150) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 51, 21);
                                uint64_t t151 = r149;
                                gm_l7 = t151;
                            }
                            goto ix120;
ie120:;
                            uint64_t t153 = gm_l7;
                            uint64_t t154 = (uintptr_t)t153;
                            uint64_t t155 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t154 >= (uintptr_t)(t155)) gm_trap_index(t154, t155, gm_srceoep1gquqdsq, 52, 27);
                            uint8_t t156 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t154 * 1));
                            uint8_t t157 = ((t156 == ((uint8_t)42ULL)));
                            if (!t157) goto ie152;
                            gm_b16_8 t158 = gm_l0;
                            gm_b16_8 t159 = t158;
                            void * t160 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t159)));
                            uint64_t t161 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t159) + 8);
                            uint64_t t162 = gm_l7;
                            uint64_t r163; bool o164 = __builtin_add_overflow(t162, ((uint64_t)1ULL), &r163);
                            if (o164) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 52, 53);
                            uint64_t t165 = r163;
                            uint64_t t166 = (uintptr_t)t165;
                            gm_tqs5p4n2tq19v t167;
                            if (t166 < t161) {
                            uint8_t t168 = GM_LD(uint8_t, ((uint8_t *)t160 + t166 * 1));
                            gm_tqs5p4n2tq19v t169;
                            GM_ST(uint8_t, ((uint8_t *)&t169), t168);
                            ((uint8_t *)&t169)[1] = 1;
                                t167 = t169;
                            } else {
                            gm_tqs5p4n2tq19v t170;
                            memset(&t170, 0, sizeof t170);
                                t167 = t170;
                            }
                            gm_tqs5p4n2tq19v t171;
                            GM_ST(uint8_t, ((uint8_t *)&t171), ((uint8_t)47ULL));
                            ((uint8_t *)&t171)[1] = 1;
                            gm_tqs5p4n2tq19v t172 = t167;
                            gm_tqs5p4n2tq19v t173 = t171;
                            uint8_t t174 = (gm_eqb44cb3cf1n(((uint8_t *)&t172), ((uint8_t *)&t173)));
                            if (!t174) goto ie152;
                            {
                                int32_t t176 = gm_l6;
                                int32_t r177; bool o178 = __builtin_sub_overflow(t176, ((int32_t)1LL), &r177);
                                if (o178) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 53, 21);
                                int32_t t179 = r177;
                                gm_l6 = t179;
                            }
                            {
                                uint64_t t180 = gm_l7;
                                uint64_t r181; bool o182 = __builtin_add_overflow(t180, ((uint64_t)2ULL), &r181);
                                if (o182) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 54, 21);
                                uint64_t t183 = r181;
                                gm_l7 = t183;
                            }
                            goto ix152;
ie152:;
                            {
                                uint64_t t185 = gm_l7;
                                uint64_t r186; bool o187 = __builtin_add_overflow(t185, ((uint64_t)1ULL), &r186);
                                if (o187) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 56, 21);
                                uint64_t t188 = r186;
                                gm_l7 = t188;
                            }
ix152:;
ix120:;
                        }
lc112:;
                    }
lb112:;
                }
                {
                    void * t189 = gm_l1;
                    uint64_t t190 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t191 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t192 = gm_l2;
                    uint64_t t193 = (uintptr_t)t192;
                    uint64_t t194 = gm_l7;
                    uint64_t t195 = (uintptr_t)t194;
                    if (t193 > t195 || t195 > (uintptr_t)t190) gm_trap_slice(t193, t195, t190, gm_srceoep1gquqdsq, 59, 31);
                    gm_b16_8 t196;
                    GM_ST(void *, (uint8_t *)&t196, (void *)(((uint8_t *)t191 + t193 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(t195 - t193));
                    gm_tj63kbqqj2092 t197;
                    GM_ST(uint8_t, (uint8_t *)&t197 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t197 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t197 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t197 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t197 + 32, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)4ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t197 + 8), t196);
                    gm_tftb1odc739t5 t198 = gm_f_push_1e500nc_4(t189, &t197);
                    gm_tftb1odc739t5 t199 = t198;
                    if (((((uint8_t *)&t199))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t199) + 1, gm_srceoep1gquqdsq, 59, 13);
                    gm_b0_1 t200 = GM_LD(gm_b0_1, (((uint8_t *)&t199) + 1));
                }
                {
                    uint64_t t201 = gm_l7;
                    gm_l2 = t201;
                }
                {
                    uint64_t t202 = gm_l2;
                    gm_l3 = t202;
                }
                goto ix87;
ie87:;
                {
                    uint64_t t204 = gm_l2;
                    uint64_t r205; bool o206 = __builtin_add_overflow(t204, ((uint64_t)1ULL), &r205);
                    if (o206) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 63, 13);
                    uint64_t t207 = r205;
                    gm_l2 = t207;
                }
ix87:;
ix40:;
ix26:;
ix12:;
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_would_merge_44g1m96(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_would_merge_44g1m96 F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint8_t gm_l2; /* x */
    uint8_t gm_l3; /* y */
    gm_b16_8 gm_l8; /* first */
    gm_b16_8 gm_l9; /* second */
    uint64_t gm_l10; /* count */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l14; /* piece */
    GM_STACK_CHECK(&F, 1229u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) {
            uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            t3 = t7;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t9 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_srceoep1gquqdsq, 78, 12);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srceoep1gquqdsq, 78, 10);
        uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
        gm_l2 = t16;
    }
    {
        uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srceoep1gquqdsq, 79, 10);
        uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t17 * 1));
        gm_l3 = t19;
    }
    {
        gm_b0_1 t20;
    }
    {
        uint8_t t22 = 0;
        gm_b16_8 t23 = GM_LD(gm_b16_8, gm_c3dq62rrli9);
        uint8_t t24 = gm_l2;
        gm_tf5srh4mp4b3i t25 = gm_f_index_of_hdeg351(t23, t24);
        gm_tf5srh4mp4b3i t26 = t25;
        uint8_t t27;
        if (((((uint8_t *)&t26))[8] != 0)) goto mn29;
        t27 = ((uint8_t)0);
        goto me28;
mn29:;
        t27 = ((uint8_t)1);
        goto me28;
mn30:;
        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 81, 8);
me28:;
        if (!t27) {
            gm_b16_8 t31 = GM_LD(gm_b16_8, gm_c3dq62rrli9);
            uint8_t t32 = gm_l3;
            gm_tf5srh4mp4b3i t33 = gm_f_index_of_hdeg351(t31, t32);
            gm_tf5srh4mp4b3i t34 = t33;
            uint8_t t35;
            if (((((uint8_t *)&t34))[8] != 0)) goto mn37;
            t35 = ((uint8_t)0);
            goto me36;
mn37:;
            t35 = ((uint8_t)1);
            goto me36;
mn38:;
            gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 81, 40);
me36:;
            t22 = t35;
        } else t22 = 1;
        if (!t22) goto ie21;
        {
            uint8_t t40 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        gm_f_new_jr5ejs0((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t42 = (void *)((uint8_t *)&F.l4);
        gm_t3mf58gv9kf3n t43 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t42));
        gm_t3mf58gv9kf3n t44 = t43;
        gm_node wn45 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t44, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        {
            gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, (&wn45), NULL);
            if (gm_unw.kind) {
                goto gm_ll47;
            }
        }
        {
            void * t49 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t50 = gm_l0;
            gm_tftb1odc739t5 t51 = gm_f_extend_0cs85gs(t49, t50);
            gm_tftb1odc739t5 t52 = t51;
            if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srceoep1gquqdsq, 86, 9);
            gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
        }
        {
            void * t54 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t55 = gm_l1;
            gm_tftb1odc739t5 t56 = gm_f_extend_0cs85gs(t54, t55);
            gm_tftb1odc739t5 t57 = t56;
            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srceoep1gquqdsq, 87, 9);
            gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
        }
        {
            gm_f_diagnostics_n3kkhf4((void *)&F.l6, (void *)NULL, (&wn45), NULL);
            if (gm_unw.kind) {
                goto gm_ll47;
            }
        }
        {
            gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t60 = t59;
            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
            void * t62 = (void *)((uint8_t *)&F.l6);
            gm_f_lex_dc8mbmf((void *)&F.l7, (void *)NULL, (&wn45), NULL, ((uint32_t)0ULL), t61, t62);
            if (gm_unw.kind) {
                goto gm_ll47;
            }
        }
        {
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(0));
            gm_l8 = t63;
        }
        {
            gm_b16_8 t64;
            GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(0));
            gm_l9 = t64;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            {
                gm_b16_8 t66 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l7) + 0) + 0));
                gm_l11 = t66;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t68 = gm_l12;
                    uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t70 = (t68 < t69);
                    if (!t70) goto lb67;
                    {
                        uint64_t t73 = gm_l12;
                        uint64_t t74 = (uintptr_t)t73;
                        uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        gm_tm4pc0emhrv1g t78 = GM_LD(gm_tm4pc0emhrv1g, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t74 * 48));
                        F.l13 = t78;
                        {
                            gm_t3fcbuehh3bs2 t81 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)&F.l13) + 0));
                            gm_t23tohc57ebhi t82;
                            gm_t3fcbuehh3bs2 t83;
                            GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t83 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t83 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t83 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)9ULL));
                            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t82 + 0, t83);
                            gm_t3fcbuehh3bs2 t84;
                            GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t84 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t84 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t84 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)10ULL));
                            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t82 + 32, t84);
                            gm_t23tohc57ebhi t85 = t82;
                            uint8_t t86 = 0;
                            gm_t3fcbuehh3bs2 t87 = t81;
                            if (gm_eqt7ftn1841u(((uint8_t *)&t85) + 0, ((uint8_t *)&t87))) t86 = 1;
                            if (gm_eqt7ftn1841u(((uint8_t *)&t85) + 32, ((uint8_t *)&t87))) t86 = 1;
                            uint8_t t88 = (uint8_t)(t86);
                            if (!t88) goto ie80;
                            {
                                goto lc67;
                            }
                            goto ix80;
ie80:;
ix80:;
                        }
                        {
                            uint64_t t90 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                            void * t91 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)));
                            uint32_t t92 = GM_LD(uint32_t, ((((uint8_t *)&F.l13) + 32) + 4));
                            uint64_t t93 = (uint64_t)t92;
                            uint64_t t94 = (uintptr_t)t93;
                            uint32_t t95 = GM_LD(uint32_t, ((((uint8_t *)&F.l13) + 32) + 8));
                            uint64_t t96 = (uint64_t)t95;
                            uint64_t t97 = (uintptr_t)t96;
                            if (t94 > t97 || t97 > (uintptr_t)t90) gm_trap_slice(t94, t97, t90, gm_srceoep1gquqdsq, 95, 22);
                            gm_b16_8 t98;
                            GM_ST(void *, (uint8_t *)&t98, (void *)(((uint8_t *)t91 + t94 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(t97 - t94));
                            gm_l14 = t98;
                        }
                        {
                            uint64_t t100 = gm_l10;
                            uint8_t t101 = ((t100 == ((uint64_t)0ULL)));
                            if (!t101) goto ie99;
                            {
                                gm_b16_8 t103 = gm_l14;
                                gm_b16_8 t104 = t103;
                                gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                                gm_l8 = t105;
                            }
                            goto ix99;
ie99:;
ix99:;
                        }
                        {
                            uint64_t t107 = gm_l10;
                            uint8_t t108 = ((t107 == ((uint64_t)1ULL)));
                            if (!t108) goto ie106;
                            {
                                gm_b16_8 t110 = gm_l14;
                                gm_b16_8 t111 = t110;
                                gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                                gm_l9 = t112;
                            }
                            goto ix106;
ie106:;
ix106:;
                        }
                        {
                            uint64_t t113 = gm_l10;
                            uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                            if (o115) gm_trap("integer overflow in `+`", gm_srceoep1gquqdsq, 98, 13);
                            uint64_t t116 = r114;
                            gm_l10 = t116;
                        }
                        goto me76;
mn77:;
                        gm_trap("no match arm matched the scrutinee", gm_srceoep1gquqdsq, 93, 9);
me76:;
                    }
lc67:;
                    {
                        uint64_t t118 = gm_l12;
                        uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                        uint64_t t121 = r119;
                        gm_l12 = t121;
                    }
                }
lb67:;
            }
        }
        {
            uint8_t t122 = 0;
            uint8_t t123 = 0;
            uint8_t t124 = 0;
            uint64_t t125 = gm_l10;
            uint8_t t126 = (!(t125 == ((uint64_t)2ULL)));
            if (!t126) {
                gm_b16_8 t127 = gm_l8;
                gm_b16_8 t128 = gm_l0;
                gm_b16_8 t129 = t127;
                gm_b16_8 t130 = t128;
                uint8_t t131 = (!gm_eq0iqiubfm7e(((uint8_t *)&t129), ((uint8_t *)&t130)));
                t124 = t131;
            } else t124 = 1;
            if (!t124) {
                gm_b16_8 t132 = gm_l9;
                gm_b16_8 t133 = gm_l1;
                gm_b16_8 t134 = t132;
                gm_b16_8 t135 = t133;
                uint8_t t136 = (!gm_eq0iqiubfm7e(((uint8_t *)&t134), ((uint8_t *)&t135)));
                t123 = t136;
            } else t123 = 1;
            if (!t123) {
                void * t137 = (void *)((uint8_t *)&F.l6);
                uint8_t t138 = gm_f_has_errors_if7ai0m(t137);
                t122 = t138;
            } else t122 = 1;
            uint8_t t139 = 0;
            F.ret = t122;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t139;
            goto gm_ll47;
        }
        goto gm_ls140;
gm_ll47:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we46; }
        goto gm_ld1_1;
gm_ls140:;
we46:;
    }
    {
        void * t141 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_sejdpqv(t141);
    }
    goto gm_ls142;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t143 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_sejdpqv(t143);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls142:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
