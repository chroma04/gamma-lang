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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csve18dtm6mt9d { void * f0; void * f1; void * f2; } gm_csve18dtm6mt9d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; } f1; } gm_t8c5h7u09n10a;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; uint32_t f2; uint8_t pe[4]; } gm_tk7kkm8e69l5f;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct gm_cs2niem4rnpi3m { gm_cseahvg8v7p0vs f0; gm_cseahvg8v7p0vs f1; } gm_cs2niem4rnpi3m;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csve18dtm6mt9d) == 24 && _Alignof(gm_csve18dtm6mt9d) == 8 && offsetof(gm_csve18dtm6mt9d, f0) == 0 && offsetof(gm_csve18dtm6mt9d, f1) == 8 && offsetof(gm_csve18dtm6mt9d, f2) == 16, "layout of `Arm`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_b16_8 f3; } gm_t26d2uc07m5o2;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; } gm_t2p74ki8sl4an;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; uint64_t f3; gm_b16_8 f4; uint64_t f5; uint64_t f6; } gm_t6s59i0k0msk8;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint8_t f3; uint8_t p4[3]; uint32_t f4; } gm_t84tkl8vf34ko;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t8c5h7u09n10a) == 16 && _Alignof(gm_t8c5h7u09n10a) == 8 && offsetof(gm_t8c5h7u09n10a, f0) == 0 && offsetof(gm_t8c5h7u09n10a, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_tk7kkm8e69l5f f0; uint8_t f1; uint8_t pe[7]; } gm_ti2sc22vlafb3;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; gm_tl874t2atnlh3 f3; } gm_tis7ttivmj935;
typedef struct { uint64_t f0; gm_tf5srh4mp4b3i f1; } gm_tk4qs2jduhfn8;
_Static_assert(sizeof(gm_tk7kkm8e69l5f) == 24 && _Alignof(gm_tk7kkm8e69l5f) == 8 && offsetof(gm_tk7kkm8e69l5f, f0) == 0 && offsetof(gm_tk7kkm8e69l5f, f1) == 8 && offsetof(gm_tk7kkm8e69l5f, f2) == 16, "layout of `Deferred`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t f3; uint8_t pe[3]; } gm_tkrkmsnb6m1b7;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tllos8achgacq;
typedef struct { uint64_t f0; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_tm9oc6v1he136;
typedef struct { gm_b16_8 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tndsf0a4f4lj0;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t f3; uint8_t pe[2]; } gm_tppdoi1slt2pr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_tl874t2atnlh3 f2; gm_b16_8 f3; } gm_tt384v6n8ojib;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_tt7dbdq4n1egc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_cs2niem4rnpi3m) == 24 && _Alignof(gm_cs2niem4rnpi3m) == 4 && offsetof(gm_cs2niem4rnpi3m, f0) == 0 && offsetof(gm_cs2niem4rnpi3m, f1) == 12, "layout of `Ref`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_tiilmfd16v3fc f0; gm_b16_8 f1; } gm_t1cs2fjnrqap1;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; gm_tkn42u7na3t81 v1; } f1; } gm_t1qr0p6kln6tf;
_Static_assert(sizeof(gm_t26d2uc07m5o2) == 48 && _Alignof(gm_t26d2uc07m5o2) == 8 && offsetof(gm_t26d2uc07m5o2, f0) == 0 && offsetof(gm_t26d2uc07m5o2, f1) == 16 && offsetof(gm_t26d2uc07m5o2, f2) == 28 && offsetof(gm_t26d2uc07m5o2, f3) == 32, "layout of `(name: string, span: Span, vis: Visibility, what: string)`");
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t2p74ki8sl4an) == 24 && _Alignof(gm_t2p74ki8sl4an) == 4 && offsetof(gm_t2p74ki8sl4an, f0) == 0 && offsetof(gm_t2p74ki8sl4an, f1) == 12, "layout of `(Span, Span)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t6s59i0k0msk8) == 72 && _Alignof(gm_t6s59i0k0msk8) == 8 && offsetof(gm_t6s59i0k0msk8, f0) == 0 && offsetof(gm_t6s59i0k0msk8, f1) == 8 && offsetof(gm_t6s59i0k0msk8, f2) == 24 && offsetof(gm_t6s59i0k0msk8, f3) == 32 && offsetof(gm_t6s59i0k0msk8, f4) == 40 && offsetof(gm_t6s59i0k0msk8, f5) == 56 && offsetof(gm_t6s59i0k0msk8, f6) == 64, "layout of `(u64, string, u64, u64, string, u64, u64)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t84tkl8vf34ko) == 56 && _Alignof(gm_t84tkl8vf34ko) == 8 && offsetof(gm_t84tkl8vf34ko, f0) == 0 && offsetof(gm_t84tkl8vf34ko, f1) == 16 && offsetof(gm_t84tkl8vf34ko, f2) == 32 && offsetof(gm_t84tkl8vf34ko, f3) == 48 && offsetof(gm_t84tkl8vf34ko, f4) == 52, "layout of `Decl`");
typedef struct { gm_tk9n3vlila9qg f0; uint8_t f1; uint8_t pe[7]; } gm_ta2tdjm589013;
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tk4qs2jduhfn8 v1; uint64_t v2; void * v3; } f1; } gm_tdkjirf1g83ud;
typedef struct { gm_tnnjeerolos0a f0; gm_trqfvmjtt4sij f1; } gm_tdtrcq4ju76rh;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_ti2sc22vlafb3) == 32 && _Alignof(gm_ti2sc22vlafb3) == 8 && offsetof(gm_ti2sc22vlafb3, f0) == 0 && offsetof(gm_ti2sc22vlafb3, f1) == 24, "layout of `?Deferred`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tis7ttivmj935) == 56 && _Alignof(gm_tis7ttivmj935) == 8 && offsetof(gm_tis7ttivmj935, f0) == 0 && offsetof(gm_tis7ttivmj935, f1) == 16 && offsetof(gm_tis7ttivmj935, f2) == 32 && offsetof(gm_tis7ttivmj935, f3) == 44, "layout of `(span: Span, name: string, local: Span, origin: Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tppdoi1slt2pr v0; gm_t08olq97rscn3 v1; gm_tt384v6n8ojib v2; gm_tt7dbdq4n1egc v3; gm_toceffb697p0u v4; gm_tl874t2atnlh3 v5; gm_t26d2uc07m5o2 v6; gm_tllos8achgacq v7; gm_tkrkmsnb6m1b7 v8; gm_tis7ttivmj935 v9; gm_tis7ttivmj935 v10; } f1; } gm_tj5f8vajf35o7;
_Static_assert(sizeof(gm_tk4qs2jduhfn8) == 24 && _Alignof(gm_tk4qs2jduhfn8) == 8 && offsetof(gm_tk4qs2jduhfn8, f0) == 0 && offsetof(gm_tk4qs2jduhfn8, f1) == 8, "layout of `(seen: u64, deadline: ?u64)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; gm_th2lqbo77pvsc f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_th2lqbo77pvsc f7; } gm_tk50ot32aqf1o;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tkrkmsnb6m1b7) == 48 && _Alignof(gm_tkrkmsnb6m1b7) == 8 && offsetof(gm_tkrkmsnb6m1b7, f0) == 0 && offsetof(gm_tkrkmsnb6m1b7, f1) == 16 && offsetof(gm_tkrkmsnb6m1b7, f2) == 32 && offsetof(gm_tkrkmsnb6m1b7, f3) == 44, "layout of `(span: Span, name: string, catch_all: Span, is_result: bool)`");
_Static_assert(sizeof(gm_tllos8achgacq) == 64 && _Alignof(gm_tllos8achgacq) == 8 && offsetof(gm_tllos8achgacq, f0) == 0 && offsetof(gm_tllos8achgacq, f1) == 16 && offsetof(gm_tllos8achgacq, f2) == 32 && offsetof(gm_tllos8achgacq, f3) == 48, "layout of `(span: Span, name: string, local: Span, why: string)`");
_Static_assert(sizeof(gm_tm9oc6v1he136) == 32 && _Alignof(gm_tm9oc6v1he136) == 8 && offsetof(gm_tm9oc6v1he136, f0) == 0 && offsetof(gm_tm9oc6v1he136, f1) == 8 && offsetof(gm_tm9oc6v1he136, f2) == 24, "layout of `TemplateInstance`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_th2lqbo77pvsc f2; } gm_tmf08dt0mom0q;
typedef struct { uint64_t f0; gm_trqfvmjtt4sij f1; } gm_tmupb6k0asuti;
_Static_assert(sizeof(gm_tndsf0a4f4lj0) == 48 && _Alignof(gm_tndsf0a4f4lj0) == 8 && offsetof(gm_tndsf0a4f4lj0, f0) == 0 && offsetof(gm_tndsf0a4f4lj0, f1) == 16 && offsetof(gm_tndsf0a4f4lj0, f2) == 24 && offsetof(gm_tndsf0a4f4lj0, f3) == 32, "layout of `Arm`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t08olq97rscn3 f1; } gm_tp9ftj4p31ha1;
typedef struct { gm_th2lqbo77pvsc f0; void * f1; } gm_tpouv9r7lrri2;
_Static_assert(sizeof(gm_tppdoi1slt2pr) == 32 && _Alignof(gm_tppdoi1slt2pr) == 8 && offsetof(gm_tppdoi1slt2pr, f0) == 0 && offsetof(gm_tppdoi1slt2pr, f1) == 16 && offsetof(gm_tppdoi1slt2pr, f2) == 28 && offsetof(gm_tppdoi1slt2pr, f3) == 29, "layout of `(name: string, span: Span, param: bool, assigned: bool)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tt384v6n8ojib) == 56 && _Alignof(gm_tt384v6n8ojib) == 8 && offsetof(gm_tt384v6n8ojib, f0) == 0 && offsetof(gm_tt384v6n8ojib, f1) == 16 && offsetof(gm_tt384v6n8ojib, f2) == 28 && offsetof(gm_tt384v6n8ojib, f3) == 40, "layout of `(name: string, span: Span, item: Span, what: string)`");
_Static_assert(sizeof(gm_tt7dbdq4n1egc) == 28 && _Alignof(gm_tt7dbdq4n1egc) == 4 && offsetof(gm_tt7dbdq4n1egc, f0) == 0 && offsetof(gm_tt7dbdq4n1egc, f1) == 12 && offsetof(gm_tt7dbdq4n1egc, f2) == 24, "layout of `(span: Span, operand: Span, same: bool)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1cs2fjnrqap1) == 40 && _Alignof(gm_t1cs2fjnrqap1) == 8 && offsetof(gm_t1cs2fjnrqap1, f0) == 0 && offsetof(gm_t1cs2fjnrqap1, f1) == 24, "layout of `HandlerLit`");
_Static_assert(sizeof(gm_t1qr0p6kln6tf) == 12 && _Alignof(gm_t1qr0p6kln6tf) == 4 && offsetof(gm_t1qr0p6kln6tf, f0) == 0 && offsetof(gm_t1qr0p6kln6tf, f1) == 4, "layout of `LabelValue`");
typedef struct { gm_b16_8 f0; gm_tmf08dt0mom0q f1; } gm_t3rj5o15e922s;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t f2; uint8_t p3[3]; gm_t1qr0p6kln6tf f3; uint8_t pe[4]; } gm_t8enl5g7b22jb;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tdkjirf1g83ud f1; uint8_t f2; uint8_t pe[7]; } gm_t9jnd88rq7ul8;
_Static_assert(sizeof(gm_ta2tdjm589013) == 48 && _Alignof(gm_ta2tdjm589013) == 8 && offsetof(gm_ta2tdjm589013, f0) == 0 && offsetof(gm_ta2tdjm589013, f1) == 40, "layout of `?StackItem`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
_Static_assert(sizeof(gm_tdkjirf1g83ud) == 32 && _Alignof(gm_tdkjirf1g83ud) == 8 && offsetof(gm_tdkjirf1g83ud, f0) == 0 && offsetof(gm_tdkjirf1g83ud, f1) == 8, "layout of `ThreadState`");
_Static_assert(sizeof(gm_tdtrcq4ju76rh) == 48 && _Alignof(gm_tdtrcq4ju76rh) == 8 && offsetof(gm_tdtrcq4ju76rh, f0) == 0 && offsetof(gm_tdtrcq4ju76rh, f1) == 8, "layout of `ParamData`");
_Static_assert(sizeof(gm_tj5f8vajf35o7) == 72 && _Alignof(gm_tj5f8vajf35o7) == 8 && offsetof(gm_tj5f8vajf35o7, f0) == 0 && offsetof(gm_tj5f8vajf35o7, f1) == 8, "layout of `Finding`");
_Static_assert(sizeof(gm_tk50ot32aqf1o) == 136 && _Alignof(gm_tk50ot32aqf1o) == 8 && offsetof(gm_tk50ot32aqf1o, f0) == 0 && offsetof(gm_tk50ot32aqf1o, f1) == 8 && offsetof(gm_tk50ot32aqf1o, f2) == 24 && offsetof(gm_tk50ot32aqf1o, f3) == 40 && offsetof(gm_tk50ot32aqf1o, f4) == 64 && offsetof(gm_tk50ot32aqf1o, f5) == 80 && offsetof(gm_tk50ot32aqf1o, f6) == 96 && offsetof(gm_tk50ot32aqf1o, f7) == 112, "layout of `Decl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
_Static_assert(sizeof(gm_tmf08dt0mom0q) == 104 && _Alignof(gm_tmf08dt0mom0q) == 8 && offsetof(gm_tmf08dt0mom0q, f0) == 0 && offsetof(gm_tmf08dt0mom0q, f1) == 40 && offsetof(gm_tmf08dt0mom0q, f2) == 80, "layout of `Refs`");
_Static_assert(sizeof(gm_tmupb6k0asuti) == 48 && _Alignof(gm_tmupb6k0asuti) == 8 && offsetof(gm_tmupb6k0asuti, f0) == 0 && offsetof(gm_tmupb6k0asuti, f1) == 8, "layout of `(i: usize, x: List((name: string, def: string)))`");
_Static_assert(sizeof(gm_tp9ftj4p31ha1) == 40 && _Alignof(gm_tp9ftj4p31ha1) == 8 && offsetof(gm_tp9ftj4p31ha1, f0) == 0 && offsetof(gm_tp9ftj4p31ha1, f1) == 8, "layout of `(fn_id: u32, name: Ident)`");
_Static_assert(sizeof(gm_tpouv9r7lrri2) == 32 && _Alignof(gm_tpouv9r7lrri2) == 8 && offsetof(gm_tpouv9r7lrri2, f0) == 0 && offsetof(gm_tpouv9r7lrri2, f1) == 24, "layout of `SpreadPart`");
_Static_assert(sizeof(gm_t3rj5o15e922s) == 120 && _Alignof(gm_t3rj5o15e922s) == 8 && offsetof(gm_t3rj5o15e922s, f0) == 0 && offsetof(gm_t3rj5o15e922s, f1) == 16, "layout of `UrlRefs`");
_Static_assert(sizeof(gm_t8enl5g7b22jb) == 48 && _Alignof(gm_t8enl5g7b22jb) == 8 && offsetof(gm_t8enl5g7b22jb, f0) == 0 && offsetof(gm_t8enl5g7b22jb, f1) == 24 && offsetof(gm_t8enl5g7b22jb, f2) == 28 && offsetof(gm_t8enl5g7b22jb, f3) == 32, "layout of `LabelInfo`");
_Static_assert(sizeof(gm_t9jnd88rq7ul8) == 48 && _Alignof(gm_t9jnd88rq7ul8) == 8 && offsetof(gm_t9jnd88rq7ul8, f0) == 0 && offsetof(gm_t9jnd88rq7ul8, f1) == 8 && offsetof(gm_t9jnd88rq7ul8, f2) == 40, "layout of `ThreadRec`");
typedef struct { gm_t8enl5g7b22jb f0; uint8_t f1; uint8_t pe[7]; } gm_te5gbhu3us3k4;
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
typedef struct { gm_t08olq97rscn3 f0; uint64_t f1; gm_tvpnktv5fo8nl f2; gm_t8c5h7u09n10a f3; } gm_t52j636u3vgk3;
_Static_assert(sizeof(gm_te5gbhu3us3k4) == 56 && _Alignof(gm_te5gbhu3us3k4) == 8 && offsetof(gm_te5gbhu3us3k4, f0) == 0 && offsetof(gm_te5gbhu3us3k4, f1) == 48, "layout of `?LabelInfo`");
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t52j636u3vgk3) == 216 && _Alignof(gm_t52j636u3vgk3) == 8 && offsetof(gm_t52j636u3vgk3, f0) == 0 && offsetof(gm_t52j636u3vgk3, f1) == 32 && offsetof(gm_t52j636u3vgk3, f2) == 40 && offsetof(gm_t52j636u3vgk3, f3) == 200, "layout of `Decl`");
struct gm_fr_clear_rm1b3rc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_rqatj1i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_bgn5i9p {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_r70pji5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_rjjscji {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_02ob3ti {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3agh8t1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5ml3jk6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5qsgea9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6716rob {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_94ocj9l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dvofb6k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ifmr7ka {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_lrnbi8e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nje5c0u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nme78np {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pbvkpvo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_b7l80f8 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bcj49nd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bf8dj7f {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bpm3e45 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_br4lm90 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bvkp9ir {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_r36qhtc {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_r8qc0er {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rb5mlsd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rbj3fr6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rc6kc0u {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rdfjble {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ri1fn5p {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rif31rv {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rk274m7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rk274m7_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rl2scuf {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rp32tbu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_pop_bmhfk7r {
    gm_frame h;
    gm_ti2sc22vlafb3 ret;
};
struct gm_fr_pop_rnfkbb9 {
    gm_frame h;
    gm_tk9n3vlila9qg l2; /* x */
    gm_ta2tdjm589013 ret;
};
struct gm_fr_pop_ruvva9t {
    gm_frame h;
    gm_t8enl5g7b22jb l2; /* x */
    gm_te5gbhu3us3k4 ret;
};
struct gm_fr_push_b9c0fdf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bb3f68j {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bn7ud1g {
    gm_frame h;
    gm_t9jnd88rq7ul8 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k {
    gm_frame h;
    gm_tndsf0a4f4lj0 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bs5eajd {
    gm_frame h;
    gm_tmupb6k0asuti l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bsig5q3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bth3krn {
    gm_frame h;
    gm_tdtrcq4ju76rh l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bu0mlkm {
    gm_frame h;
    gm_t1cs2fjnrqap1 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bu2hcel {
    gm_frame h;
    gm_tp9ftj4p31ha1 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r2k6rjf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r2k6rjf_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r4haklv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r5pdr8o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r6o112l {
    gm_frame h;
    gm_t3rj5o15e922s l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rb04a8h {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rda4o9n {
    gm_frame h;
    gm_t84tkl8vf34ko l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rda4o9n_2 {
    gm_frame h;
    gm_t52j636u3vgk3 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rda4o9n_3 {
    gm_frame h;
    gm_tk50ot32aqf1o l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rlpbnuq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rluch9r {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_roqi806 {
    gm_frame h;
    gm_tdgfduqjheqdm l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rqlbv1e {
    gm_frame h;
    gm_tj5f8vajf35o7 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_b5mmd35 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_bfalbh4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_bg5jjth {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_bnsp8p9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_bqmbgmv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_br5lm9l {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_bslikk9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_r51vrc8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_r51vrc8_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_r51vrc8_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_r690fpr {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_r8ccoha {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_r9l0iqd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_rb1b3ic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_rgq4vgc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_rjs5nj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ro7k701 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_rtkd3qo {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_b3knhd6 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_b9o3mne {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_baasn56 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_bq1tl8a {
    gm_frame h;
    void * l0; /* l */
    gm_t6s59i0k0msk8 l1; /* args */
    uint8_t l2; /* failed */
    gm_t6s59i0k0msk8 l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_bvpfm1k {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_r7au1ks {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_r83e4t6 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_rbbkc88 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_rc0tik1 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_rcan3t8 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_rkgcjv5 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_rv9cdbb {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
__attribute__((aligned(4))) void gm_f_clear_rm1b3rc(void * a0);
__attribute__((aligned(4))) void gm_f_clear_rqatj1i(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_bgn5i9p(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_rjjscji(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1di25gg(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_28d0d11(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2k4643t(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_4(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4q8glfd(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4u7p3q6(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_537clvp(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5j6fqpv(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8lf24cm(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo_2(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8td41o0(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_93nt26c(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_asvj6q6(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_behvl9q(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_br8psrk(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dq2isu2(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f8s372n(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ft2q3st(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hh33a3d(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3_2(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jdnmbru(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jvoelso(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_l26i0t5(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lcqki20(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_m2f8gtr(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_m3oe7t3(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n7sb6c8(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n7sb6c8_2(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n7sb6c8_3(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pbl8eob(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q870hn3(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tslvjbf(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_v7c9k0s(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vt6lar6(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_02ob3ti(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_3agh8t1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_5ml3jk6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_5qsgea9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_6716rob(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_94ocj9l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_dvofb6k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_ifmr7ka(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_lrnbi8e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_nje5c0u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_nme78np(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_pbvkpvo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_list_b7l80f8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_bcj49nd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_bf8dj7f(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_bpm3e45(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_br4lm90(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_r36qhtc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_r8qc0er(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_rb5mlsd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_rbj3fr6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_rc6kc0u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_rdfjble(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_ri1fn5p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_rif31rv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_rk274m7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_rk274m7_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_rl2scuf(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_rp32tbu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_bmhfk7r(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_ti2sc22vlafb3 gm_f_pop_bmhfk7r_c(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_rnfkbb9(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_ta2tdjm589013 gm_f_pop_rnfkbb9_c(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_te5gbhu3us3k4 gm_f_pop_ruvva9t_c(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_b9c0fdf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_b9c0fdf_c(void * a0, gm_tm9oc6v1he136 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bb3f68j(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bb3f68j_c(void * a0, gm_t08olq97rscn3 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bn7ud1g(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bn7ud1g_c(void * a0, gm_t9jnd88rq7ul8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_2_c(void * a0, gm_tcmkompbl09il a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_3_c(void * a0, gm_csve18dtm6mt9d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_4(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_4_c(void * a0, gm_csve18dtm6mt9d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_c(void * a0, gm_tndsf0a4f4lj0 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bs5eajd(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bs5eajd_c(void * a0, gm_tmupb6k0asuti a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bsig5q3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bsig5q3_c(void * a0, gm_tpouv9r7lrri2 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bth3krn(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bth3krn_c(void * a0, gm_tdtrcq4ju76rh a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bu0mlkm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bu0mlkm_c(void * a0, gm_t1cs2fjnrqap1 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bu2hcel(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bu2hcel_c(void * a0, gm_tp9ftj4p31ha1 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r2k6rjf_2(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r4haklv(void * a0, gm_t9805qtonbsag a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r5pdr8o(void * a0, gm_t2i8ne5ibd7v4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r6o112l(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r6o112l_c(void * a0, gm_t3rj5o15e922s a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rb04a8h(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rb04a8h_c(void * a0, gm_cs2niem4rnpi3m a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n_2_c(void * a0, gm_t52j636u3vgk3 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n_3_c(void * a0, gm_tk50ot32aqf1o a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n_c(void * a0, gm_t84tkl8vf34ko a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rlpbnuq(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rlpbnuq_c(void * a0, gm_trqfvmjtt4sij a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_roqi806(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_roqi806_c(void * a0, gm_tdgfduqjheqdm a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rqlbv1e(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rqlbv1e_c(void * a0, gm_tj5f8vajf35o7 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0s9hfc3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3n5udep(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_46rs2i6(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_46rs2i6_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_46rs2i6_3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8l7j2k6(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9a4nica(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9ibjpen(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_b5mmd35(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bfalbh4(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bg5jjth(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bnsp8p9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bqmbgmv(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_br5lm9l(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bslikk9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_g0f0rl7(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_4(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ibbj0r1(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_l875bgq(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_lq604tk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_nl0tf27(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ontu3di(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q6son6o(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_qpqf461(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8_3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r690fpr(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r8ccoha(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r9l0iqd(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_rb1b3ic(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_rgq4vgc(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_rjs5nj4(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ro7k701(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_rtkd3qo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ub69oip(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_b3knhd6(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_b3knhd6_c(void * a0, gm_t6j6smj7qoctu a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_b9o3mne(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_baasn56(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bq1tl8a(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bq1tl8a_c(void * a0, gm_t6s59i0k0msk8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bvpfm1k(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_r7au1ks(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_r7au1ks_c(void * a0, gm_t6j6smj7qoctu a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_r83e4t6(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rbbkc88(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rc0tik1(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rcan3t8(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rkgcjv5(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rv9cdbb(void * a0, gm_tnpifv20irnph a1);
GM_NOINLINE bool gm_pf0aqhubr4up(void *code, void *env, gm_node *cx, const void *d, uintptr_t n);
GM_NOINLINE bool gm_pfk8csn7us23(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s);
extern const uint8_t gm_s2urait735ofq[5];
extern const uint8_t gm_s5ivi0sts5foi[9];
extern const uint8_t gm_s5vakuk6ui4kj[9];
extern const uint8_t gm_s6b2qpq3tgsj8[12];
extern const uint8_t gm_s6s4ngfaj87vv[5];
extern const uint8_t gm_s7qcnd652ae6d[25];
extern const uint8_t gm_sbm0k1981kgpb[40];
extern const uint8_t gm_sdo9t6m49dsco[5];
extern const uint8_t gm_seh12nehvgmhj[12];
extern const uint8_t gm_sf0p9o7g7fr6l[7];
extern const uint8_t gm_sg398rgmck67b[4];
extern const uint8_t gm_sgankvlme129s[3];
extern const uint8_t gm_skfdo9qggtrbp[6];
extern const uint8_t gm_sm3sqa0e3amks[9];
extern const uint8_t gm_snf877edekrff[11];
extern const uint8_t gm_snl36eh5psull[7];
extern const uint8_t gm_snr5o5tak6405[4];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sokpg5qd8r9va[20];
extern const uint8_t gm_spm9ae4g2b2fd[14];
extern const uint8_t gm_sqrh6i8djv6b4[7];
extern const uint8_t gm_sr4utq83odb88[8];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_stshdb5m6rl63[5];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_tyb2nno7n67r;
extern const gm_type gm_tybihgc69jv3;
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_bmhfk7r(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_bmhfk7r F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_tk7kkm8e69l5f gm_l2; /* x */
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
            gm_ti2sc22vlafb3 t8;
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
        gm_tk7kkm8e69l5f t17 = GM_LD(gm_tk7kkm8e69l5f, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 24));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_tk7kkm8e69l5f t29 = gm_l2;
        gm_ti2sc22vlafb3 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 28, 0);
        GM_ST(gm_tk7kkm8e69l5f, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[24] = 1;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_rnfkbb9(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_rnfkbb9 F;
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
            gm_ta2tdjm589013 t8;
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
        gm_tk9n3vlila9qg t17 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 40));
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
        gm_tk9n3vlila9qg t29 = F.l2;
        gm_ta2tdjm589013 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 44, 0);
        GM_ST(gm_tk9n3vlila9qg, ((uint8_t *)&t30), t29);
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
inline __attribute__((aligned(4))) void gm_f_clear_rm1b3rc(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_rm1b3rc F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 72u);
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
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 137, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_rqatj1i(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_rqatj1i F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 72u);
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
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 137, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_bgn5i9p(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_bgn5i9p F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 16));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, ((((uint8_t *)t7) + 24) + 8));
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 16));
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, ((((uint8_t *)t10) + 24) + 0));
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            void * t14 = t13;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 16));
            uint64_t r17; bool o18 = __builtin_mul_overflow(((uint64_t)40ULL), t16, &r17);
            if (o18) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 143, 64);
            uint64_t t19 = r17;
            void * t20 = t9;
            gm_b0_1 t21;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t20))(t11, t14, t19, ((uint64_t)4ULL));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l0;
        uint8_t *t23 = (((uint8_t *)t22) + 0);
        gm_b16_8 t24 = gm_f_empty_slice_m2f8gtr();
        GM_ST(gm_b16_8, t23, t24);
    }
    {
        void * t25 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t25) + 16), ((uint64_t)0ULL));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_r70pji5 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 16));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, ((((uint8_t *)t7) + 24) + 8));
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 16));
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, ((((uint8_t *)t10) + 24) + 0));
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            void * t14 = t13;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 16));
            uint64_t r17; bool o18 = __builtin_mul_overflow(((uint64_t)1ULL), t16, &r17);
            if (o18) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 143, 64);
            uint64_t t19 = r17;
            void * t20 = t9;
            gm_b0_1 t21;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t20))(t11, t14, t19, ((uint64_t)1ULL));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l0;
        uint8_t *t23 = (((uint8_t *)t22) + 0);
        gm_b16_8 t24 = gm_f_empty_slice_ijtp9lo();
        GM_ST(gm_b16_8, t23, t24);
    }
    {
        void * t25 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t25) + 16), ((uint64_t)0ULL));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_rjjscji(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_rjjscji F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 16));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, ((((uint8_t *)t7) + 24) + 8));
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 16));
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, ((((uint8_t *)t10) + 24) + 0));
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            void * t14 = t13;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 16));
            uint64_t r17; bool o18 = __builtin_mul_overflow(((uint64_t)8ULL), t16, &r17);
            if (o18) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 143, 64);
            uint64_t t19 = r17;
            void * t20 = t9;
            gm_b0_1 t21;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t20))(t11, t14, t19, ((uint64_t)8ULL));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l0;
        uint8_t *t23 = (((uint8_t *)t22) + 0);
        gm_b16_8 t24 = gm_f_empty_slice_br8psrk();
        GM_ST(gm_b16_8, t23, t24);
    }
    {
        void * t25 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t25) + 16), ((uint64_t)0ULL));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_02ob3ti(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_02ob3ti F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_r7au1ks *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_r7au1ks *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_r7au1ks *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3agh8t1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_3agh8t1 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_rc0tik1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_rc0tik1 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_rc0tik1 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5ml3jk6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_5ml3jk6 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_r83e4t6 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_r83e4t6 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_r83e4t6 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5qsgea9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_5qsgea9 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_baasn56 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_baasn56 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_baasn56 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6716rob(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_6716rob F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_rv9cdbb *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_rv9cdbb *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_rv9cdbb *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_94ocj9l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_94ocj9l F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_b3knhd6 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_b3knhd6 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_b3knhd6 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dvofb6k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_dvofb6k F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_b9o3mne *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_b9o3mne *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_b9o3mne *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ifmr7ka(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ifmr7ka F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_rcan3t8 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_rcan3t8 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_rcan3t8 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lrnbi8e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_lrnbi8e F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_bvpfm1k *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_bvpfm1k *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_bvpfm1k *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nje5c0u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_nje5c0u F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_rbbkc88 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_rbbkc88 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_rbbkc88 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nme78np(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_nme78np F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_bq1tl8a *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_bq1tl8a *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_bq1tl8a *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pbvkpvo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pbvkpvo F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_rkgcjv5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_rkgcjv5 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_rkgcjv5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) void gm_f_list_b7l80f8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_b7l80f8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vt6lar6();
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
inline __attribute__((aligned(4))) void gm_f_list_bcj49nd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bcj49nd F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_m3oe7t3();
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
inline __attribute__((aligned(4))) void gm_f_list_bf8dj7f(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bf8dj7f F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_m2f8gtr();
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
inline __attribute__((aligned(4))) void gm_f_list_bpm3e45(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bpm3e45 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_537clvp();
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
inline __attribute__((aligned(4))) void gm_f_list_br4lm90(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_br4lm90 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_l26i0t5();
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
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bvkp9ir F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8jt91p4();
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
inline __attribute__((aligned(4))) void gm_f_list_r36qhtc(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_r36qhtc F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_28d0d11();
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
inline __attribute__((aligned(4))) void gm_f_list_r8qc0er(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_r8qc0er F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ft2q3st();
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
inline __attribute__((aligned(4))) void gm_f_list_rb5mlsd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rb5mlsd F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hh33a3d();
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
inline __attribute__((aligned(4))) void gm_f_list_rbj3fr6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rbj3fr6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_4u7p3q6();
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
inline __attribute__((aligned(4))) void gm_f_list_rc6kc0u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rc6kc0u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_asvj6q6();
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
inline __attribute__((aligned(4))) void gm_f_list_rdfjble(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rdfjble F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_v7c9k0s();
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
inline __attribute__((aligned(4))) void gm_f_list_ri1fn5p(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ri1fn5p F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_f8s372n();
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
inline __attribute__((aligned(4))) void gm_f_list_rif31rv(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rif31rv F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_pbl8eob();
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
inline __attribute__((aligned(4))) void gm_f_list_rk274m7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rk274m7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_jbu9fo3();
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
inline __attribute__((aligned(4))) void gm_f_list_rl2scuf(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rl2scuf F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_4q8glfd();
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
inline __attribute__((aligned(4))) void gm_f_list_rp32tbu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rp32tbu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_jvoelso();
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
extern __attribute__((aligned(4))) void gm_f_pop_bmhfk7r(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_ti2sc22vlafb3 gm_f_pop_bmhfk7r_c(void * a0) { gm_ti2sc22vlafb3 gm_r; gm_f_pop_bmhfk7r(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_pop_rnfkbb9(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_ta2tdjm589013 gm_f_pop_rnfkbb9_c(void * a0) { gm_ta2tdjm589013 gm_r; gm_f_pop_rnfkbb9(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_pop_ruvva9t(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_te5gbhu3us3k4 gm_f_pop_ruvva9t_c(void * a0) { gm_te5gbhu3us3k4 gm_r; gm_f_pop_ruvva9t(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_b9c0fdf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_b9c0fdf F;
    void * gm_l0; /* l */
    gm_tm9oc6v1he136 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 329u);
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
            gm_b16_8 t11 = gm_f_empty_slice_tslvjbf();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_ub69oip(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tm9oc6v1he136 t32 = GM_LD(gm_tm9oc6v1he136, ((uint8_t *)a1));
        GM_ST(gm_tm9oc6v1he136, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 32), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_b9c0fdf_c(void * a0, gm_tm9oc6v1he136 a1) { return gm_f_push_b9c0fdf(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bb3f68j(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bb3f68j F;
    void * gm_l0; /* l */
    gm_t08olq97rscn3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 329u);
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
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_rgq4vgc(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t08olq97rscn3 t32 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a1));
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 32), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bb3f68j_c(void * a0, gm_t08olq97rscn3 a1) { return gm_f_push_bb3f68j(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bn7ud1g(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bn7ud1g F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t11 = gm_f_empty_slice_5j6fqpv();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_l875bgq(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t9jnd88rq7ul8 t32 = GM_LD(gm_t9jnd88rq7ul8, ((uint8_t *)a1));
        GM_ST(gm_t9jnd88rq7ul8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 48), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bn7ud1g_c(void * a0, gm_t9jnd88rq7ul8 a1) { return gm_f_push_bn7ud1g(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t11 = gm_f_empty_slice_3v2gjd4();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_h9tbucc(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tndsf0a4f4lj0 t32 = F.l1;
        GM_ST(gm_tndsf0a4f4lj0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 48), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_2 F;
    void * gm_l0; /* l */
    gm_tcmkompbl09il gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t11 = gm_f_empty_slice_3v2gjd4_2();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_h9tbucc_2(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tcmkompbl09il t32 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)a1));
        GM_ST(gm_tcmkompbl09il, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 24), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_2_c(void * a0, gm_tcmkompbl09il a1) { return gm_f_push_bpovr2k_2(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_3 F;
    void * gm_l0; /* l */
    gm_tbbqefuhv2r8h gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_h9tbucc_3(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tbbqefuhv2r8h t32 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)a1));
        GM_ST(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 24), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_3_c(void * a0, gm_csve18dtm6mt9d a1) { return gm_f_push_bpovr2k_3(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_4 F;
    void * gm_l0; /* l */
    gm_tbbqefuhv2r8h gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t11 = gm_f_empty_slice_3v2gjd4_4();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_h9tbucc_4(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tbbqefuhv2r8h t32 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)a1));
        GM_ST(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 24), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_4_c(void * a0, gm_csve18dtm6mt9d a1) { return gm_f_push_bpovr2k_4(a0, &a1); }
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_c(void * a0, gm_tndsf0a4f4lj0 a1) { return gm_f_push_bpovr2k(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bs5eajd(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bs5eajd F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t11 = gm_f_empty_slice_93nt26c();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_ontu3di(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tmupb6k0asuti t32 = GM_LD(gm_tmupb6k0asuti, ((uint8_t *)a1));
        GM_ST(gm_tmupb6k0asuti, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 48), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bs5eajd_c(void * a0, gm_tmupb6k0asuti a1) { return gm_f_push_bs5eajd(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bsig5q3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bsig5q3 F;
    void * gm_l0; /* l */
    gm_tpouv9r7lrri2 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 329u);
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
            gm_b16_8 t11 = gm_f_empty_slice_8lf24cm();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_nl0tf27(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tpouv9r7lrri2 t32 = GM_LD(gm_tpouv9r7lrri2, ((uint8_t *)a1));
        GM_ST(gm_tpouv9r7lrri2, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 32), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bsig5q3_c(void * a0, gm_tpouv9r7lrri2 a1) { return gm_f_push_bsig5q3(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bth3krn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bth3krn F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t11 = gm_f_empty_slice_2k4643t();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_lq604tk(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tdtrcq4ju76rh t32 = GM_LD(gm_tdtrcq4ju76rh, ((uint8_t *)a1));
        GM_ST(gm_tdtrcq4ju76rh, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 48), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bth3krn_c(void * a0, gm_tdtrcq4ju76rh a1) { return gm_f_push_bth3krn(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bu0mlkm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bu0mlkm F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 305u);
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
            gm_b16_8 t11 = gm_f_empty_slice_c3qnk0a();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_8l7j2k6(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t1cs2fjnrqap1 t32 = GM_LD(gm_t1cs2fjnrqap1, ((uint8_t *)a1));
        GM_ST(gm_t1cs2fjnrqap1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 40), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bu0mlkm_c(void * a0, gm_t1cs2fjnrqap1 a1) { return gm_f_push_bu0mlkm(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bu2hcel(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bu2hcel F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 305u);
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
            gm_b16_8 t11 = gm_f_empty_slice_dq2isu2();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_9ibjpen(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tp9ftj4p31ha1 t32 = GM_LD(gm_tp9ftj4p31ha1, ((uint8_t *)a1));
        GM_ST(gm_tp9ftj4p31ha1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 40), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bu2hcel_c(void * a0, gm_tp9ftj4p31ha1 a1) { return gm_f_push_bu2hcel(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_r2k6rjf F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 281u);
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
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_j7fb9h1(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        void * t32 = gm_l1;
        GM_ST(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 8), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r2k6rjf_2(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_r2k6rjf_2 F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 281u);
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
            gm_b16_8 t11 = gm_f_empty_slice_8q3kspo_2();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_j7fb9h1_2(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        void * t32 = gm_l1;
        GM_ST(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 8), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r4haklv(void * a0, gm_t9805qtonbsag a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_r4haklv F;
    void * gm_l0; /* l */
    gm_t9805qtonbsag gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 281u);
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
            gm_b16_8 t11 = gm_f_empty_slice_hh33a3d();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_0s9hfc3(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t9805qtonbsag t32 = gm_l1;
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 8), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r5pdr8o(void * a0, gm_t2i8ne5ibd7v4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_r5pdr8o F;
    void * gm_l0; /* l */
    gm_t2i8ne5ibd7v4 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 297u);
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
            gm_b16_8 t11 = gm_f_empty_slice_q870hn3();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_g0f0rl7(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t2i8ne5ibd7v4 t32 = gm_l1;
        GM_ST(gm_t2i8ne5ibd7v4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 16), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r6o112l(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_r6o112l F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 385u);
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
            gm_b16_8 t11 = gm_f_empty_slice_lcqki20();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_ibbj0r1(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 120)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t3rj5o15e922s t32 = GM_LD(gm_t3rj5o15e922s, ((uint8_t *)a1));
        GM_ST(gm_t3rj5o15e922s, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 120), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r6o112l_c(void * a0, gm_t3rj5o15e922s a1) { return gm_f_push_r6o112l(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rb04a8h(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rb04a8h F;
    void * gm_l0; /* l */
    gm_t2p74ki8sl4an gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_q6son6o(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t2p74ki8sl4an t32 = GM_LD(gm_t2p74ki8sl4an, ((uint8_t *)a1));
        GM_ST(gm_t2p74ki8sl4an, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 24), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rb04a8h_c(void * a0, gm_cs2niem4rnpi3m a1) { return gm_f_push_rb04a8h(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rda4o9n F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 321u);
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
            gm_b16_8 t11 = gm_f_empty_slice_n7sb6c8();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_46rs2i6(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 56)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t84tkl8vf34ko t32 = GM_LD(gm_t84tkl8vf34ko, ((uint8_t *)a1));
        GM_ST(gm_t84tkl8vf34ko, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 56), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rda4o9n_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 481u);
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
            gm_b16_8 t11 = gm_f_empty_slice_n7sb6c8_2();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_46rs2i6_2(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 216)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t52j636u3vgk3 t32 = GM_LD(gm_t52j636u3vgk3, ((uint8_t *)a1));
        GM_ST(gm_t52j636u3vgk3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 216), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n_2_c(void * a0, gm_t52j636u3vgk3 a1) { return gm_f_push_rda4o9n_2(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rda4o9n_3 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 401u);
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
            gm_b16_8 t11 = gm_f_empty_slice_n7sb6c8_3();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_46rs2i6_3(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 136)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tk50ot32aqf1o t32 = GM_LD(gm_tk50ot32aqf1o, ((uint8_t *)a1));
        GM_ST(gm_tk50ot32aqf1o, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 136), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n_3_c(void * a0, gm_tk50ot32aqf1o a1) { return gm_f_push_rda4o9n_3(a0, &a1); }
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n_c(void * a0, gm_t84tkl8vf34ko a1) { return gm_f_push_rda4o9n(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rlpbnuq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rlpbnuq F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 305u);
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
            gm_b16_8 t11 = gm_f_empty_slice_1di25gg();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_qpqf461(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_trqfvmjtt4sij t32 = F.l1;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 40), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rlpbnuq_c(void * a0, gm_trqfvmjtt4sij a1) { return gm_f_push_rlpbnuq(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rluch9r F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 297u);
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
            gm_b16_8 t11 = gm_f_empty_slice_2p7d2kv();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_bslikk9(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_b16_8 t32 = gm_l1;
        GM_ST(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 16), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_roqi806(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_roqi806 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 329u);
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
            gm_b16_8 t11 = gm_f_empty_slice_1fu8oa9();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_9a4nica(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 64)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tdgfduqjheqdm t32 = GM_LD(gm_tdgfduqjheqdm, ((uint8_t *)a1));
        GM_ST(gm_tdgfduqjheqdm, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 64), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_roqi806_c(void * a0, gm_tdgfduqjheqdm a1) { return gm_f_push_roqi806(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rqlbv1e(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rqlbv1e F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 337u);
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
            gm_b16_8 t11 = gm_f_empty_slice_behvl9q();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_3n5udep(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 72)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tj5f8vajf35o7 t32 = F.l1;
        GM_ST(gm_tj5f8vajf35o7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 72), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rqlbv1e_c(void * a0, gm_tj5f8vajf35o7 a1) { return gm_f_push_rqlbv1e(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_b5mmd35(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_b5mmd35 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)88ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bfalbh4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_bfalbh4 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)80ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bg5jjth(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_bg5jjth F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
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
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)128ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bqmbgmv(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_bqmbgmv F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_br5lm9l(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_br5lm9l F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bslikk9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_bslikk9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_r51vrc8 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_r51vrc8_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)384ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r51vrc8_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_r51vrc8_3 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r690fpr(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_r690fpr F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r8ccoha(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_r8ccoha F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r9l0iqd(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_r9l0iqd F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_rb1b3ic(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_rb1b3ic F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)20ULL), ((uint64_t)4ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
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
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_rjs5nj4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_rjs5nj4 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)104ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ro7k701(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ro7k701 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_rtkd3qo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_rtkd3qo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_srcrgc9bkquf6nr, 27, 23);
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_b3knhd6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_b3knhd6 F;
    gm_t6j6smj7qoctu gm_l1; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_94ocj9l, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_sokpg5qd8r9va, 19)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_sf0p9o7g7fr6l, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_tybihgc69jv3, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_sg398rgmck67b, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
        F.ret = t19;
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_b3knhd6_c(void * a0, gm_t6j6smj7qoctu a1) { return gm_f_write_b3knhd6(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_b9o3mne(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_b9o3mne F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_dvofb6k, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sm3sqa0e3amks, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sdo9t6m49dsco, 4)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_baasn56(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_baasn56 F;
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_5qsgea9, (void *)&F.h, cx});
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sbm0k1981kgpb, 39)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_tyb2nno7n67r, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_snr5o5tak6405, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bq1tl8a(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_bq1tl8a F;
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6s59i0k0msk8 t3 = GM_LD(gm_t6s59i0k0msk8, ((uint8_t *)a1));
            F.l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_nme78np, (void *)&F.h, cx});
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 0));
            uint64_t t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
            gm_b16_8 t8 = t7;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 24));
            uint64_t t10 = t9;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 32));
            uint64_t t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
            gm_b16_8 t14 = t13;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 56));
            uint64_t t16 = t15;
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 64));
            uint64_t t18 = t17;
            gm_closure t19 = t4;
            gm_node *ccx20 = t19.cx;
            if (gm_pf0aqhubr4up(t19.code, t19.env, ccx20, gm_spm9ae4g2b2fd, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t19.code, t19.env, ccx20, &gm_tyb2nno7n67r, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t19.code, t19.env, ccx20, gm_stshdb5m6rl63, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t19.code, t19.env, ccx20, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t19.code, t19.env, ccx20, gm_skfdo9qggtrbp, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t19.code, t19.env, ccx20, &gm_tyb2nno7n67r, ((uint8_t *)&t10), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t19.code, t19.env, ccx20, gm_sqrh6i8djv6b4, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t19.code, t19.env, ccx20, &gm_tyb2nno7n67r, ((uint8_t *)&t12), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t19.code, t19.env, ccx20, gm_s6s4ngfaj87vv, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t19.code, t19.env, ccx20, &gm_ty0iqiubfm7e, ((uint8_t *)&t14), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t19.code, t19.env, ccx20, gm_skfdo9qggtrbp, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t19.code, t19.env, ccx20, &gm_tyb2nno7n67r, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t19.code, t19.env, ccx20, gm_sqrh6i8djv6b4, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t19.code, t19.env, ccx20, &gm_tyb2nno7n67r, ((uint8_t *)&t18), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t19.code, t19.env, ccx20, gm_snr5o5tak6405, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t25;
            ((uint8_t *)&t25)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 1), t24);
            gm_tftb1odc739t5 t26 = t25;
            uint8_t t27 = ((((uint8_t *)&t26))[0] != 0);
            F.ret = t25;
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        gm_b0_1 t28;
        gm_tftb1odc739t5 t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        ((uint8_t *)&t29)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t29 + 1), t28);
        gm_tftb1odc739t5 t30 = t29;
        uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
        F.ret = t29;
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bq1tl8a_c(void * a0, gm_t6s59i0k0msk8 a1) { return gm_f_write_bq1tl8a(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bvpfm1k(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_bvpfm1k F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_lrnbi8e, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_snf877edekrff, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_r7au1ks(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_r7au1ks F;
    gm_t6j6smj7qoctu gm_l1; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_02ob3ti, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_s7qcnd652ae6d, 24)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_s5ivi0sts5foi, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_tybihgc69jv3, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_sg398rgmck67b, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
        F.ret = t19;
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_r7au1ks_c(void * a0, gm_t6j6smj7qoctu a1) { return gm_f_write_r7au1ks(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_r83e4t6(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_r83e4t6 F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_5ml3jk6, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sr4utq83odb88, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rbbkc88(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_rbbkc88 F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_nje5c0u, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_s6b2qpq3tgsj8, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rc0tik1(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_rc0tik1 F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_3agh8t1, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sgankvlme129s, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_s2urait735ofq, 4)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rcan3t8(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_rcan3t8 F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_ifmr7ka, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_snl36eh5psull, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rkgcjv5(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_rkgcjv5 F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_pbvkpvo, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_s5vakuk6ui4kj, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rv9cdbb(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_rv9cdbb F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_6716rob, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_seh12nehvgmhj, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
