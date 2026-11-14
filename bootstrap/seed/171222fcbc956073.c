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
typedef struct gm_csfq64m1m1rt6m { int64_t f0; } gm_csfq64m1m1rt6m;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint8_t f2; uint8_t pe[3]; } gm_tfc64ef2cjub8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[7]; uint64_t f1; } gm_tphtgnn9vgkcv;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csfq64m1m1rt6m) == 8 && _Alignof(gm_csfq64m1m1rt6m) == 8 && offsetof(gm_csfq64m1m1rt6m, f0) == 0, "layout of `Dir`");
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; } gm_t2p74ki8sl4an;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; int64_t f1; gm_tm98ap4112hee f2; } gm_t856vjvr00kbi;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_tap3g6eptfeor;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
_Static_assert(sizeof(gm_tfc64ef2cjub8) == 12 && _Alignof(gm_tfc64ef2cjub8) == 4 && offsetof(gm_tfc64ef2cjub8, f0) == 0 && offsetof(gm_tfc64ef2cjub8, f1) == 4 && offsetof(gm_tfc64ef2cjub8, f2) == 8, "layout of `Slot(u32, bool)`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_tphtgnn9vgkcv) == 16 && _Alignof(gm_tphtgnn9vgkcv) == 8 && offsetof(gm_tphtgnn9vgkcv, f0) == 0 && offsetof(gm_tphtgnn9vgkcv, f1) == 8, "layout of `(found: bool, at: usize)`");
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; gm_b16_8 f2; } gm_tqhauhm6mj3ud;
typedef struct { gm_b16_8 f0; int64_t f1; gm_b16_8 f2; } gm_tqj9esv5ukti3;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_tl874t2atnlh3 f0; gm_t88rcucg1b7uk f1; } gm_trlabqsjjp0ds;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t2p74ki8sl4an) == 24 && _Alignof(gm_t2p74ki8sl4an) == 4 && offsetof(gm_t2p74ki8sl4an, f0) == 0 && offsetof(gm_t2p74ki8sl4an, f1) == 12, "layout of `(Span, Span)`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t856vjvr00kbi) == 24 && _Alignof(gm_t856vjvr00kbi) == 8 && offsetof(gm_t856vjvr00kbi, f0) == 0 && offsetof(gm_t856vjvr00kbi, f1) == 8 && offsetof(gm_t856vjvr00kbi, f2) == 16, "layout of `Slot(i64, Child)`");
_Static_assert(sizeof(gm_tap3g6eptfeor) == 32 && _Alignof(gm_tap3g6eptfeor) == 8 && offsetof(gm_tap3g6eptfeor, f0) == 0 && offsetof(gm_tap3g6eptfeor, f1) == 8 && offsetof(gm_tap3g6eptfeor, f2) == 24, "layout of `Slot(string, u32)`");
typedef struct { uint8_t f0; uint8_t f1; uint8_t p2[2]; gm_toceffb697p0u f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t pe[1]; } gm_tb9j2bleq9j8f;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_b16_8 f0; gm_tqj9esv5ukti3 f1; } gm_tggvvh3lncvcg;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_tbgd14esf0jvi f0; uint8_t f1; uint8_t pe[7]; } gm_tnvnsr1bfvtbd;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_tiilmfd16v3fc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_to2fqbbntv85f;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
_Static_assert(sizeof(gm_tqhauhm6mj3ud) == 24 && _Alignof(gm_tqhauhm6mj3ud) == 8 && offsetof(gm_tqhauhm6mj3ud, f0) == 0 && offsetof(gm_tqhauhm6mj3ud, f1) == 4 && offsetof(gm_tqhauhm6mj3ud, f2) == 8, "layout of `Slot(u32, string)`");
_Static_assert(sizeof(gm_tqj9esv5ukti3) == 40 && _Alignof(gm_tqj9esv5ukti3) == 8 && offsetof(gm_tqj9esv5ukti3, f0) == 0 && offsetof(gm_tqj9esv5ukti3, f1) == 16 && offsetof(gm_tqj9esv5ukti3, f2) == 24, "layout of `Doc`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_b16_8 f1; gm_tqj9esv5ukti3 f2; } gm_tr51kd2g9k5cf;
_Static_assert(sizeof(gm_trlabqsjjp0ds) == 16 && _Alignof(gm_trlabqsjjp0ds) == 4 && offsetof(gm_trlabqsjjp0ds, f0) == 0 && offsetof(gm_trlabqsjjp0ds, f1) == 12, "layout of `(Span, Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_tiilmfd16v3fc f1; uint32_t f2; uint8_t pe[4]; } gm_ttahj15cedals;
typedef struct { gm_t4c7ipjj9s3fp f0; } gm_ttiitul0263ld;
typedef struct { gm_tnvnsr1bfvtbd f0; } gm_t5jnq73fben73;
_Static_assert(sizeof(gm_tb9j2bleq9j8f) == 24 && _Alignof(gm_tb9j2bleq9j8f) == 4 && offsetof(gm_tb9j2bleq9j8f, f0) == 0 && offsetof(gm_tb9j2bleq9j8f, f1) == 1 && offsetof(gm_tb9j2bleq9j8f, f2) == 4 && offsetof(gm_tb9j2bleq9j8f, f3) == 20 && offsetof(gm_tb9j2bleq9j8f, f4) == 21 && offsetof(gm_tb9j2bleq9j8f, f5) == 22, "layout of `(mutable: bool, typed: bool, op: ?Span, keep_mutable: bool, fn_lit: bool, uninit: bool)`");
_Static_assert(sizeof(gm_tggvvh3lncvcg) == 56 && _Alignof(gm_tggvvh3lncvcg) == 8 && offsetof(gm_tggvvh3lncvcg, f0) == 0 && offsetof(gm_tggvvh3lncvcg, f1) == 16, "layout of `(key: string, value: Doc)`");
_Static_assert(sizeof(gm_tnvnsr1bfvtbd) == 40 && _Alignof(gm_tnvnsr1bfvtbd) == 8 && offsetof(gm_tnvnsr1bfvtbd, f0) == 0 && offsetof(gm_tnvnsr1bfvtbd, f1) == 32, "layout of `?(span: Span, text: string)`");
_Static_assert(sizeof(gm_to2fqbbntv85f) == 48 && _Alignof(gm_to2fqbbntv85f) == 8 && offsetof(gm_to2fqbbntv85f, f0) == 0 && offsetof(gm_to2fqbbntv85f, f1) == 8 && offsetof(gm_to2fqbbntv85f, f2) == 32, "layout of `Slot((u32, string), Span)`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tr51kd2g9k5cf) == 64 && _Alignof(gm_tr51kd2g9k5cf) == 8 && offsetof(gm_tr51kd2g9k5cf, f0) == 0 && offsetof(gm_tr51kd2g9k5cf, f1) == 8 && offsetof(gm_tr51kd2g9k5cf, f2) == 24, "layout of `Slot(string, Doc)`");
_Static_assert(sizeof(gm_ttahj15cedals) == 40 && _Alignof(gm_ttahj15cedals) == 8 && offsetof(gm_ttahj15cedals, f0) == 0 && offsetof(gm_ttahj15cedals, f1) == 8 && offsetof(gm_ttahj15cedals, f2) == 32, "layout of `Slot((u32, []const Ty), u32)`");
_Static_assert(sizeof(gm_ttiitul0263ld) == 56 && _Alignof(gm_ttiitul0263ld) == 8 && offsetof(gm_ttiitul0263ld, f0) == 0, "layout of `Namer`");
_Static_assert(sizeof(gm_t5jnq73fben73) == 40 && _Alignof(gm_t5jnq73fben73) == 8 && offsetof(gm_t5jnq73fben73, f0) == 0, "layout of `(fix: ?(at: Span, to: string))`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tb9j2bleq9j8f v0; gm_tbgd14esf0jvi v1; gm_t5jnq73fben73 v2; } f1; } gm_tl5vm0tcqvdvb;
_Static_assert(sizeof(gm_tl5vm0tcqvdvb) == 48 && _Alignof(gm_tl5vm0tcqvdvb) == 8 && offsetof(gm_tl5vm0tcqvdvb, f0) == 0 && offsetof(gm_tl5vm0tcqvdvb, f1) == 8, "layout of `Site`");
struct gm_fr_contains_a8d6g03 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_did_close_f9lpcgi {
    gm_frame h;
    gm_b16_8 l5; /* dir */
    gm_trqfvmjtt4sij l8; /* old */
    gm_trqfvmjtt4sij l9; /* others */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_dir_of_ho73hue {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6l50clp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_htfpvg4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_khi2i98 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oahbrct {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oq9aajs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_entries_qfi2q5v {
    gm_frame h;
    gm_tr51kd2g9k5cf l4; /* s */
    gm_b0_1 ret;
};
struct gm_fr_find_a1bt60v {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_find_ag86i9n {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_find_qrmk1ol {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_for_body_0h3rgr6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_1qm1hvv {
    gm_frame h;
    gm_tggvvh3lncvcg l0; /* <item> */
    gm_tggvvh3lncvcg l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_for_body_i2u6ls4 {
    gm_frame h;
    gm_tggvvh3lncvcg l0; /* <item> */
    gm_tggvvh3lncvcg l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_for_body_s23qjlr {
    gm_frame h;
    gm_tggvvh3lncvcg l0; /* <item> */
    gm_tggvvh3lncvcg l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_for_body_sf9naq7 {
    gm_frame h;
    gm_tggvvh3lncvcg l0; /* <item> */
    gm_tggvvh3lncvcg l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_get_ag7vhtq {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_get_ahntg11 {
    gm_frame h;
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_get_q3uemcr {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_get_qjsv97t {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_grow_a9240mf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_grow_ai5cphs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_grow_aqcfk8r {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_grow_qo0e2hr {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_hash_map_a2hplai {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_abi2bn7 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_apl3mkc {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_ql49boo {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_insert_new_a0qulo8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_insert_new_a2a8f5d {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_insert_new_a2n7fmc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_insert_new_anb2gma {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_insert_new_q01idpd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_insert_new_q9jfo6k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_insert_new_qkout7s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_key_eq_agm4agv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_key_eq_qasrbu4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_key_eq_td9pjmr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_key_hash_4r9ue8o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_7bps17u {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_a3h40i4 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_a8a5e8m {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_at8t7r1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_ckite1s {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_hebjkbg {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_nkn0rq8 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_odgtuo9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_qa443hf {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_keys_qc5bjlr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_member_names_17el54k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_open_dirs_9ud02ot {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* dirs */
    gm_trqfvmjtt4sij ret;
    gm_b0_1 w0;
};
struct gm_fr_overlays_6rdbiin {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij ret;
    gm_b0_1 w0;
};
struct gm_fr_push_302k12h {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_put_a7l7vun {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_put_amueool {
    gm_frame h;
    gm_tl5vm0tcqvdvb l2; /* v */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_put_qkbvor7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_put_qoojoqv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_put_qrf6f79 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5c6uc3s {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_uri_map_mhud6ak {
    gm_frame h;
    gm_ttiitul0263ld l1; /* m */
    gm_ttiitul0263ld ret;
    gm_b0_1 w0;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq8l669blqfv(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_a8d6g03(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_contains_a8d6g03_c(void * a0, gm_t2p74ki8sl4an a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dir_of_ho73hue(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6l50clp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_htfpvg4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_khi2i98(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oahbrct(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oq9aajs(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_s728q5p(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_s75phag(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_secjmhg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_smjejb0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_sogpj69(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_son0uk9(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_70gl1ke(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_8gjlcrm(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_9qkg6n9(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_a1bt60v(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ag86i9n(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ci1c6mk(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_dg8ttg4(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_efqiqfc(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_hjk709p(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_iegifbl(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ovslcai(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_qrmk1ol(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_r7voucn(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0h3rgr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1qm1hvv(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_7fmdm88(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2u6ls4(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_s23qjlr(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_sf9naq7(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_t3rkkf2(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ag7vhtq(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) void gm_f_get_ahntg11(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tqetrgo0q24b5 gm_f_get_ahntg11_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_q3uemcr(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_get_qjsv97t(void * a0, const void * a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_get_qjsv97t_c(void * a0, gm_tiilmfd16v3fc a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_6cul2eh(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_81srcpr(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_a9240mf(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_ai5cphs(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_aqcfk8r(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_ci6cs98(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_kpihb7f(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_qo0e2hr(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_uav5st9(void * a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_a2hplai(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_abi2bn7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_apl3mkc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_ql49boo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_insert_new_0he56fl(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_8duufql(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_9tncde1(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_a0qulo8(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_a2a8f5d(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_a2n7fmc(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_anb2gma(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_e2nhmvi(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_kmsbdni(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_l14losc(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_p16jnkm(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_q01idpd(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_q9jfo6k(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_qkout7s(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_t150iph(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_vbg72ug(void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_key_eq_agm4agv(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_key_eq_qasrbu4(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_key_eq_td9pjmr(void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_4r9ue8o(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_7bps17u(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_a3h40i4(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_a8a5e8m(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_at8t7r1(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_ckite1s(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_hebjkbg(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_nkn0rq8(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_odgtuo9(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_qa443hf(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_keys_qc5bjlr(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_keys_qc5bjlr_s31qjug(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_302k12h(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_a7l7vun(void * a0, gm_tcc1rq4n1avb2 a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_amueool(void * a0, gm_cseahvg8v7p0vs a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_amueool_c(void * a0, gm_cseahvg8v7p0vs a1, gm_tl5vm0tcqvdvb a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qkbvor7(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qoojoqv(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qrf6f79(void * a0, gm_trlabqsjjp0ds a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5c6uc3s(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline uint64_t gm_hash0iqiubfm7e(const uint8_t *v, uint64_t h);
inline uint64_t gm_hash1r633cskta(const uint8_t *v, uint64_t h);
inline uint64_t gm_hash467d5cm1d5(const uint8_t *v, uint64_t h);
inline uint64_t gm_hash8l669blqfv(const uint8_t *v, uint64_t h);
inline uint64_t gm_hash9m6mvtmgde(const uint8_t *v, uint64_t h);
inline uint64_t gm_hash9v0qhqsmrd(const uint8_t *v, uint64_t h);
inline uint64_t gm_hashb2nno7n67r(const uint8_t *v, uint64_t h);
inline uint64_t gm_hashf8lvnv0fhh(const uint8_t *v, uint64_t h);
inline uint64_t gm_hashff85ac10o3(const uint8_t *v, uint64_t h);
inline uint64_t gm_hashi1jkm8p1sd(const uint8_t *v, uint64_t h);
inline uint64_t gm_hashqar16dk5lb(const uint8_t *v, uint64_t h);
inline uint64_t gm_hashrmvoa03cpj(const uint8_t *v, uint64_t h);
inline uint64_t gm_hashsqt22rph28(const uint8_t *v, uint64_t h);
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcfpg6jsb3cfp6[];
extern const char gm_srckfibkpt681aa[];
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
inline bool gm_eq8l669blqfv(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    return true;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_a8d6g03(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_a8d6g03 F;
    void * gm_l0; /* m */
    gm_t2p74ki8sl4an gm_l1; /* k */
    GM_STACK_CHECK(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_dg8ttg4(t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dir_of_ho73hue(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_dir_of_ho73hue F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_th2lqbo77pvsc t2;
    gm_f_parent_lk0iero((void *)&t2, (void *)NULL, cx, NULL, t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t3;
    if (((((uint8_t *)&t2))[16] != 0)) {
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&t2) + 0));
        t3 = t4;
    } else {
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(0));
        t3 = t5;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6l50clp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6l50clp F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_htfpvg4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_htfpvg4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_khi2i98(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_khi2i98 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oahbrct(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oahbrct F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oq9aajs(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oq9aajs F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0h3rgr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_0h3rgr6 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_member_names_17el54k *)((&F.h)->up))->l1);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srckfibkpt681aa, 456, 37);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 456, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1qm1hvv(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_1qm1hvv F;
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tggvvh3lncvcg t3 = GM_LD(gm_tggvvh3lncvcg, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_open_dirs_9ud02ot *)((&F.h)->up))->l1);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t7 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t8 = gm_f_push_3ef746b(t5, t7);
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcfpg6jsb3cfp6, 529, 36);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 529, 5);
me1:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2u6ls4(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_i2u6ls4 F;
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tggvvh3lncvcg t3 = GM_LD(gm_tggvvh3lncvcg, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_overlays_6rdbiin *)((&F.h)->up))->l1);
        gm_t6j6smj7qoctu t6;
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 0, t7);
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l1) + 16) + 24));
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 16, t8);
        gm_tftb1odc739t5 t9 = gm_f_push_302k12h(t5, &t6);
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srcfpg6jsb3cfp6, 549, 36);
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 549, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_s23qjlr(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_s23qjlr F;
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tggvvh3lncvcg t3 = GM_LD(gm_tggvvh3lncvcg, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        void * t5 = (void *)(((uint8_t *)&((struct gm_fr_uri_map_mhud6ak *)((&F.h)->up))->l1) + 0);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l1) + 16) + 0));
        gm_tftb1odc739t5 t8 = gm_f_put_7shfu9u(t5, t6, t7);
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcfpg6jsb3cfp6, 555, 36);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 555, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_sf9naq7(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_sf9naq7 F;
    GM_STACK_CHECK(&F, 106u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tggvvh3lncvcg t3 = GM_LD(gm_tggvvh3lncvcg, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t7 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_did_close_f9lpcgi *)((&F.h)->up))->l5));
        uint8_t t9 = (gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
        if (!t9) goto ie5;
        {
            gm_b0_1 t11;
            uint8_t t12 = 0;
            ((struct gm_fr_did_close_f9lpcgi *)((&F.h)->up))->ret = t11;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t12;
            goto gm_lad;
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 448, 5);
me1:;
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ag7vhtq(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ag7vhtq F;
    void * gm_l0; /* m */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 115u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_efqiqfc(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tqs5p4n2tq19v t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 119, 12);
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 16) + 12));
        gm_tqs5p4n2tq19v t14;
        GM_ST(uint8_t, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[1] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_a2hplai(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_a2hplai F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_htfpvg4();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_abi2bn7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_abi2bn7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_6l50clp();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_apl3mkc(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_apl3mkc F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_oahbrct();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_ql49boo(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_ql49boo F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_khi2i98();
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
inline __attribute__((aligned(4))) uint8_t gm_f_key_eq_agm4agv(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_eq_agm4agv F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 110u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            void * t8 = gm_l1;
            void * t9 = t6;
            uint8_t t10;
            t10 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t9))(t7, t8);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        int64_t t13 = GM_LD(int64_t, ((uint8_t *)t12));
        void * t14 = gm_l1;
        int64_t t15 = GM_LD(int64_t, ((uint8_t *)t14));
        uint8_t t16 = ((t13 == t15));
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_key_eq_qasrbu4(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_eq_qasrbu4 F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 110u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            void * t8 = gm_l1;
            void * t9 = t6;
            uint8_t t10;
            t10 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t9))(t7, t8);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        gm_tcc1rq4n1avb2 t13 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)t12));
        void * t14 = gm_l1;
        gm_tcc1rq4n1avb2 t15 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)t14));
        uint8_t t16 = (gm_eq8l669blqfv(((uint8_t *)&t13), ((uint8_t *)&t15)));
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_key_eq_td9pjmr(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_eq_td9pjmr F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            void * t8 = gm_l1;
            void * t9 = t6;
            uint8_t t10;
            t10 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t9))(t7, t8);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)t12));
        void * t14 = gm_l1;
        uint32_t t15 = GM_LD(uint32_t, ((uint8_t *)t14));
        uint8_t t16 = ((t13 == t15));
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_4r9ue8o(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_4r9ue8o F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        gm_tiilmfd16v3fc t13 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        uint64_t t15 = gm_hashi1jkm8p1sd(((uint8_t *)&t13), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_7bps17u(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_7bps17u F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        int64_t t13 = GM_LD(int64_t, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        uint64_t t15 = gm_hashsqt22rph28(((uint8_t *)&t13), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_a3h40i4(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_a3h40i4 F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        uint64_t t15 = gm_hashqar16dk5lb(((uint8_t *)&t13), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_a8a5e8m(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_a8a5e8m F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        gm_tcc1rq4n1avb2 t13 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        uint64_t t15 = gm_hash8l669blqfv(((uint8_t *)&t13), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_at8t7r1(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_at8t7r1 F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        uint64_t t15 = gm_hash9v0qhqsmrd(((uint8_t *)&t13), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_ckite1s(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_ckite1s F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        gm_tiilmfd16v3fc t13 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        uint64_t t15 = gm_hashf8lvnv0fhh(((uint8_t *)&t13), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_hebjkbg(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_hebjkbg F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        uint64_t t15 = gm_hashb2nno7n67r(((uint8_t *)&t13), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_nkn0rq8(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_nkn0rq8 F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        uint64_t t15 = gm_hash467d5cm1d5(((uint8_t *)&t13), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_odgtuo9(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_odgtuo9 F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 138u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        uint64_t t15 = gm_hash0iqiubfm7e(((uint8_t *)&t13), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_qa443hf(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_qa443hf F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 138u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        gm_trlabqsjjp0ds t13 = GM_LD(gm_trlabqsjjp0ds, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        uint64_t t15 = gm_hash1r633cskta(((uint8_t *)&t13), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_302k12h(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_302k12h F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_oq9aajs();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_5c6uc3s(t10, ((uint64_t)1ULL), t12);
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
        gm_t6j6smj7qoctu t21 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)t20 + t18 * 32), t21);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5c6uc3s(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5c6uc3s F;
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
inline uint64_t gm_hash0iqiubfm7e(const uint8_t *v, uint64_t h) {
    uint64_t n = GM_LD(uintptr_t, v + 8); const uint8_t *d = GM_LD(void *, v);
    h = gm_mix64(h, n);
    h = gm_mix_bytes(h, d, n);
    return h;
}
inline uint64_t gm_hash1r633cskta(const uint8_t *v, uint64_t h) {
    h = gm_hashff85ac10o3(v + 0, h);
    h = gm_hash9m6mvtmgde(v + 12, h);
    return h;
}
inline uint64_t gm_hash467d5cm1d5(const uint8_t *v, uint64_t h) {
    return gm_mix64(h, (uint64_t)GM_LD(uint32_t, v));
}
inline uint64_t gm_hash8l669blqfv(const uint8_t *v, uint64_t h) {
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 0));
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 4));
    return h;
}
inline uint64_t gm_hash9m6mvtmgde(const uint8_t *v, uint64_t h) {
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 0));
    return h;
}
inline uint64_t gm_hash9v0qhqsmrd(const uint8_t *v, uint64_t h) {
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 0));
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 4));
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 8));
    return h;
}
inline uint64_t gm_hashb2nno7n67r(const uint8_t *v, uint64_t h) {
    return gm_mix64(h, (uint64_t)GM_LD(uint64_t, v));
}
inline uint64_t gm_hashf8lvnv0fhh(const uint8_t *v, uint64_t h) {
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 0));
    h = gm_hashrmvoa03cpj(v + 8, h);
    return h;
}
inline uint64_t gm_hashff85ac10o3(const uint8_t *v, uint64_t h) {
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 0));
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 4));
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 8));
    return h;
}
inline uint64_t gm_hashi1jkm8p1sd(const uint8_t *v, uint64_t h) {
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 0));
    h = gm_hash0iqiubfm7e(v + 8, h);
    return h;
}
inline uint64_t gm_hashqar16dk5lb(const uint8_t *v, uint64_t h) {
    h = gm_hash0iqiubfm7e(v + 0, h);
    h = gm_hash0iqiubfm7e(v + 16, h);
    return h;
}
inline uint64_t gm_hashrmvoa03cpj(const uint8_t *v, uint64_t h) {
    uint64_t n = GM_LD(uintptr_t, v + 8); const uint8_t *d = GM_LD(void *, v);
    h = gm_mix64(h, n);
    for (uint64_t i = 0; i < n; i++) h = gm_hash9m6mvtmgde(d + i * 4, h);
    return h;
}
inline uint64_t gm_hashsqt22rph28(const uint8_t *v, uint64_t h) {
    return gm_mix64(h, (uint64_t)(int64_t)GM_LD(int64_t, v));
}
extern __attribute__((aligned(4))) uint8_t gm_f_contains_a8d6g03(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_contains_a8d6g03_c(void * a0, gm_t2p74ki8sl4an a1) { return gm_f_contains_a8d6g03(a0, &a1); }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_entries_qfi2q5v F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 267u);
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
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l3 = t14;
                    {
                        uint64_t t16 = gm_l3;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcbsc23is03ttm, 185, 14);
                        gm_tr51kd2g9k5cf t20 = GM_LD(gm_tr51kd2g9k5cf, ((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)) + t17 * 64));
                        F.l4 = t20;
                    }
                    {
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t23 = ((t22 == ((uint8_t)1ULL)));
                        if (!t23) goto ie21;
                        {
                            gm_tggvvh3lncvcg t25;
                            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                            GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
                            memcpy((uint8_t *)&t25 + 16, (((uint8_t *)&F.l4) + 24), 40);
                            gm_node *pn27 = gm_find(cx, 1408113935);
                            if (!pn27) gm_trap("no handler installed for effect `Yield`", gm_srcbsc23is03ttm, 186, 30);
                            gm_b0_1 t28;
                            t28 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, const void *))pn27->clauses[0])(pn27->env, pn27->hctx, pn27, &t25);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 184, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t30 = gm_l2;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l2 = t33;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_s728q5p(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_entries_qfi2q5v F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 267u);
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
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l3 = t14;
                    {
                        uint64_t t16 = gm_l3;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcbsc23is03ttm, 185, 14);
                        gm_tr51kd2g9k5cf t20 = GM_LD(gm_tr51kd2g9k5cf, ((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)) + t17 * 64));
                        F.l4 = t20;
                    }
                    {
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t23 = ((t22 == ((uint8_t)1ULL)));
                        if (!t23) goto ie21;
                        {
                            gm_tggvvh3lncvcg t25;
                            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                            GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
                            memcpy((uint8_t *)&t25 + 16, (((uint8_t *)&F.l4) + 24), 40);
                            gm_b0_1 t27 = gm_f_for_body_sf9naq7(cx->env, cx->hctx, cx, &t25);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 184, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l2;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l2 = t32;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_s75phag(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_entries_qfi2q5v F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 267u);
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
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l3 = t14;
                    {
                        uint64_t t16 = gm_l3;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcbsc23is03ttm, 185, 14);
                        gm_tr51kd2g9k5cf t20 = GM_LD(gm_tr51kd2g9k5cf, ((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)) + t17 * 64));
                        F.l4 = t20;
                    }
                    {
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t23 = ((t22 == ((uint8_t)1ULL)));
                        if (!t23) goto ie21;
                        {
                            gm_tggvvh3lncvcg t25;
                            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                            GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
                            memcpy((uint8_t *)&t25 + 16, (((uint8_t *)&F.l4) + 24), 40);
                            gm_b0_1 t27 = gm_f_for_body_7fmdm88(cx->env, cx->hctx, cx, &t25);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 184, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l2;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l2 = t32;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_secjmhg(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_entries_qfi2q5v F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 267u);
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
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l3 = t14;
                    {
                        uint64_t t16 = gm_l3;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcbsc23is03ttm, 185, 14);
                        gm_tr51kd2g9k5cf t20 = GM_LD(gm_tr51kd2g9k5cf, ((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)) + t17 * 64));
                        F.l4 = t20;
                    }
                    {
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t23 = ((t22 == ((uint8_t)1ULL)));
                        if (!t23) goto ie21;
                        {
                            gm_tggvvh3lncvcg t25;
                            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                            GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
                            memcpy((uint8_t *)&t25 + 16, (((uint8_t *)&F.l4) + 24), 40);
                            gm_b0_1 t27 = gm_f_for_body_i2u6ls4(cx->env, cx->hctx, cx, &t25);
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 184, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l2;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l2 = t32;
                }
            }
lb5:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_smjejb0(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_entries_qfi2q5v F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 267u);
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
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l3 = t14;
                    {
                        uint64_t t16 = gm_l3;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcbsc23is03ttm, 185, 14);
                        gm_tr51kd2g9k5cf t20 = GM_LD(gm_tr51kd2g9k5cf, ((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)) + t17 * 64));
                        F.l4 = t20;
                    }
                    {
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t23 = ((t22 == ((uint8_t)1ULL)));
                        if (!t23) goto ie21;
                        {
                            gm_tggvvh3lncvcg t25;
                            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                            GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
                            memcpy((uint8_t *)&t25 + 16, (((uint8_t *)&F.l4) + 24), 40);
                            gm_b0_1 t27 = gm_f_for_body_1qm1hvv(cx->env, cx->hctx, cx, &t25);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 184, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l2;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l2 = t32;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_sogpj69(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_entries_qfi2q5v F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 267u);
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
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l3 = t14;
                    {
                        uint64_t t16 = gm_l3;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcbsc23is03ttm, 185, 14);
                        gm_tr51kd2g9k5cf t20 = GM_LD(gm_tr51kd2g9k5cf, ((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)) + t17 * 64));
                        F.l4 = t20;
                    }
                    {
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t23 = ((t22 == ((uint8_t)1ULL)));
                        if (!t23) goto ie21;
                        {
                            gm_tggvvh3lncvcg t25;
                            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                            GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
                            memcpy((uint8_t *)&t25 + 16, (((uint8_t *)&F.l4) + 24), 40);
                            gm_b0_1 t27 = gm_f_for_body_t3rkkf2(cx->env, cx->hctx, cx, &t25);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 184, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l2;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l2 = t32;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_entries_qfi2q5v_son0uk9(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_entries_qfi2q5v F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 267u);
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
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l3 = t14;
                    {
                        uint64_t t16 = gm_l3;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcbsc23is03ttm, 185, 14);
                        gm_tr51kd2g9k5cf t20 = GM_LD(gm_tr51kd2g9k5cf, ((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)) + t17 * 64));
                        F.l4 = t20;
                    }
                    {
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t23 = ((t22 == ((uint8_t)1ULL)));
                        if (!t23) goto ie21;
                        {
                            gm_tggvvh3lncvcg t25;
                            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                            GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
                            memcpy((uint8_t *)&t25 + 16, (((uint8_t *)&F.l4) + 24), 40);
                            gm_b0_1 t27 = gm_f_for_body_s23qjlr(cx->env, cx->hctx, cx, &t25);
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 184, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l2;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l2 = t32;
                }
            }
lb5:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_a1bt60v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_a1bt60v F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* s */
    GM_STACK_CHECK(&F, 345u);
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
        uint64_t t5 = gm_l2;
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
        void * t10 = gm_l1;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
        uint64_t t13 = gm_f_key_hash_7bps17u(t10, t12);
        uint64_t t14 = (uint64_t)t13;
        uint64_t t15 = gm_l2;
        uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
        if (o17) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 60, 52);
        uint64_t t18 = r16;
        uint64_t t19 = (uint64_t)(t14 & t18);
        gm_l3 = t19;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb20;
            {
                uint64_t t23 = gm_l3;
                uint64_t t24 = (uintptr_t)t23;
                void * t25 = gm_l0;
                uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 0) + 8);
                if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_srcbsc23is03ttm, 62, 15);
                void * t27 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t25) + 0)) + t24 * 24);
                gm_l4 = t27;
            }
            {
                void * t29 = gm_l4;
                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                uint8_t t31 = ((t30 == ((uint8_t)0ULL)));
                if (!t31) goto ie28;
                {
                    gm_tf5srh4mp4b3i t33;
                    memset(&t33, 0, sizeof t33);
                    uint8_t t34 = 0;
                    F.ret = t33;
                    goto gm_fret;
                }
                goto ix28;
ie28:;
ix28:;
            }
            {
                void * t36 = gm_l4;
                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t36) + 0));
                uint8_t t38 = ((t37 == ((uint8_t)1ULL)));
                if (!t38) goto ie35;
                void * t39 = gm_l4;
                void * t40 = (void *)(((uint8_t *)t39) + 8);
                void * t41 = gm_l1;
                uint8_t t42 = gm_f_key_eq_agm4agv(t40, t41);
                if (!t42) goto ie35;
                {
                    uint64_t t44 = gm_l3;
                    gm_tf5srh4mp4b3i t45;
                    GM_ST(uint8_t, (uint8_t *)&t45 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t45 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t45 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t45), t44);
                    ((uint8_t *)&t45)[8] = 1;
                    uint8_t t46 = 0;
                    F.ret = t45;
                    goto gm_fret;
                }
                goto ix35;
ie35:;
ix35:;
            }
            {
                uint64_t t47 = gm_l3;
                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                if (o49) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 65, 14);
                uint64_t t50 = r48;
                uint64_t t51 = gm_l2;
                uint64_t r52; bool o53 = __builtin_sub_overflow(t51, ((uint64_t)1ULL), &r52);
                if (o53) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 65, 24);
                uint64_t t54 = r52;
                uint64_t t55 = (uint64_t)(t50 & t54);
                gm_l3 = t55;
            }
lc20:;
        }
lb20:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ag86i9n(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_ag86i9n F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* s */
    GM_STACK_CHECK(&F, 345u);
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
        uint64_t t5 = gm_l2;
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
        void * t10 = gm_l1;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
        uint64_t t13 = gm_f_key_hash_nkn0rq8(t10, t12);
        uint64_t t14 = (uint64_t)t13;
        uint64_t t15 = gm_l2;
        uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
        if (o17) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 60, 52);
        uint64_t t18 = r16;
        uint64_t t19 = (uint64_t)(t14 & t18);
        gm_l3 = t19;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb20;
            {
                uint64_t t23 = gm_l3;
                uint64_t t24 = (uintptr_t)t23;
                void * t25 = gm_l0;
                uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 0) + 8);
                if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_srcbsc23is03ttm, 62, 15);
                void * t27 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t25) + 0)) + t24 * 24);
                gm_l4 = t27;
            }
            {
                void * t29 = gm_l4;
                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                uint8_t t31 = ((t30 == ((uint8_t)0ULL)));
                if (!t31) goto ie28;
                {
                    gm_tf5srh4mp4b3i t33;
                    memset(&t33, 0, sizeof t33);
                    uint8_t t34 = 0;
                    F.ret = t33;
                    goto gm_fret;
                }
                goto ix28;
ie28:;
ix28:;
            }
            {
                void * t36 = gm_l4;
                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t36) + 0));
                uint8_t t38 = ((t37 == ((uint8_t)1ULL)));
                if (!t38) goto ie35;
                void * t39 = gm_l4;
                void * t40 = (void *)(((uint8_t *)t39) + 4);
                void * t41 = gm_l1;
                uint8_t t42 = gm_f_key_eq_td9pjmr(t40, t41);
                if (!t42) goto ie35;
                {
                    uint64_t t44 = gm_l3;
                    gm_tf5srh4mp4b3i t45;
                    GM_ST(uint8_t, (uint8_t *)&t45 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t45 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t45 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t45), t44);
                    ((uint8_t *)&t45)[8] = 1;
                    uint8_t t46 = 0;
                    F.ret = t45;
                    goto gm_fret;
                }
                goto ix35;
ie35:;
ix35:;
            }
            {
                uint64_t t47 = gm_l3;
                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                if (o49) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 65, 14);
                uint64_t t50 = r48;
                uint64_t t51 = gm_l2;
                uint64_t r52; bool o53 = __builtin_sub_overflow(t51, ((uint64_t)1ULL), &r52);
                if (o53) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 65, 24);
                uint64_t t54 = r52;
                uint64_t t55 = (uint64_t)(t50 & t54);
                gm_l3 = t55;
            }
lc20:;
        }
lb20:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_qrmk1ol(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_qrmk1ol F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* s */
    GM_STACK_CHECK(&F, 345u);
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
        uint64_t t5 = gm_l2;
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
        void * t10 = gm_l1;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
        uint64_t t13 = gm_f_key_hash_nkn0rq8(t10, t12);
        uint64_t t14 = (uint64_t)t13;
        uint64_t t15 = gm_l2;
        uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
        if (o17) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 60, 52);
        uint64_t t18 = r16;
        uint64_t t19 = (uint64_t)(t14 & t18);
        gm_l3 = t19;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb20;
            {
                uint64_t t23 = gm_l3;
                uint64_t t24 = (uintptr_t)t23;
                void * t25 = gm_l0;
                uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 0) + 8);
                if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_srcbsc23is03ttm, 62, 15);
                void * t27 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t25) + 0)) + t24 * 72);
                gm_l4 = t27;
            }
            {
                void * t29 = gm_l4;
                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                uint8_t t31 = ((t30 == ((uint8_t)0ULL)));
                if (!t31) goto ie28;
                {
                    gm_tf5srh4mp4b3i t33;
                    memset(&t33, 0, sizeof t33);
                    uint8_t t34 = 0;
                    F.ret = t33;
                    goto gm_fret;
                }
                goto ix28;
ie28:;
ix28:;
            }
            {
                void * t36 = gm_l4;
                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t36) + 0));
                uint8_t t38 = ((t37 == ((uint8_t)1ULL)));
                if (!t38) goto ie35;
                void * t39 = gm_l4;
                void * t40 = (void *)(((uint8_t *)t39) + 4);
                void * t41 = gm_l1;
                uint8_t t42 = gm_f_key_eq_td9pjmr(t40, t41);
                if (!t42) goto ie35;
                {
                    uint64_t t44 = gm_l3;
                    gm_tf5srh4mp4b3i t45;
                    GM_ST(uint8_t, (uint8_t *)&t45 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t45 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t45 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t45), t44);
                    ((uint8_t *)&t45)[8] = 1;
                    uint8_t t46 = 0;
                    F.ret = t45;
                    goto gm_fret;
                }
                goto ix35;
ie35:;
ix35:;
            }
            {
                uint64_t t47 = gm_l3;
                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                if (o49) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 65, 14);
                uint64_t t50 = r48;
                uint64_t t51 = gm_l2;
                uint64_t r52; bool o53 = __builtin_sub_overflow(t51, ((uint64_t)1ULL), &r52);
                if (o53) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 65, 24);
                uint64_t t54 = r52;
                uint64_t t55 = (uint64_t)(t50 & t54);
                gm_l3 = t55;
            }
lc20:;
        }
lb20:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ag7vhtq(void * a0, gm_tcc1rq4n1avb2 a1);
inline __attribute__((aligned(4))) void gm_f_get_ahntg11(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ahntg11 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_r7voucn(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tqetrgo0q24b5 t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 119, 12);
        const uint8_t *t13 = (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 64) + 24);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 44, 0);
        ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out))[40] = 1;
        memmove((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 0, t13, 40);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tqetrgo0q24b5 gm_f_get_ahntg11_c(void * a0, gm_b16_8 a1) { gm_tqetrgo0q24b5 gm_r; gm_f_get_ahntg11(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_q3uemcr(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_q3uemcr F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 138u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_iegifbl(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_t9805qtonbsag t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 119, 12);
        uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 12) + 8));
        gm_t9805qtonbsag t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[4] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_toceffb697p0u gm_f_get_qjsv97t(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_qjsv97t F;
    void * gm_l0; /* m */
    gm_tiilmfd16v3fc gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 198u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_9qkg6n9(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_toceffb697p0u t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 119, 12);
        gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 48) + 32));
        gm_toceffb697p0u t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[12] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_get_qjsv97t_c(void * a0, gm_tiilmfd16v3fc a1) { return gm_f_get_qjsv97t(a0, &a1); }
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_a9240mf(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_grow_a9240mf F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* old */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* size */
    uint64_t gm_l4; /* align */
    void * gm_l5; /* raw */
    gm_b16_8 gm_l6; /* slots */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 810u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        gm_l1 = t3;
    }
    {
        uint64_t t4;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie5;
        uint64_t t9 = ((uint64_t)8ULL);
        t4 = t9;
        goto ix5;
ie5:;
        uint64_t t10;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 16));
        uint64_t r14; bool o15 = __builtin_mul_overflow(t13, ((uint64_t)2ULL), &r14);
        if (o15) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 85, 40);
        uint64_t t16 = r14;
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t18 = (t16 <= t17);
        if (!t18) goto ie11;
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t21 = t20;
        t10 = t21;
        goto ix11;
ie11:;
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r24; bool o25 = __builtin_mul_overflow(t23, ((uint64_t)2ULL), &r24);
        if (o25) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 85, 82);
        uint64_t t26 = r24;
        uint64_t t27 = t26;
        t10 = t27;
ix11:;
        t4 = t10;
ix5:;
        gm_l2 = t4;
    }
    {
        gm_l3 = ((uint64_t)64ULL);
    }
    {
        gm_l4 = ((uint64_t)8ULL);
    }
    {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, ((((uint8_t *)t28) + 40) + 8));
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, ((((uint8_t *)t31) + 40) + 0));
        uint64_t t33 = gm_l3;
        uint64_t t34 = gm_l2;
        uint64_t r35; bool o36 = __builtin_mul_overflow(t33, t34, &r35);
        if (o36) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 88, 43);
        uint64_t t37 = r35;
        uint64_t t38 = gm_l4;
        void * t39 = t30;
        void * t40;
        t40 = ((void * (*)(void *, uint64_t, uint64_t))GM_PTR_UNTAG(t39))(t32, t37, t38);
        void * t41;
        if ((GM_LD(void *, ((uint8_t *)&t40)) != 0)) {
            void * t42 = GM_LD(void *, (((uint8_t *)&t40) + 0));
            t41 = t42;
        } else {
            gm_t9qkh0rsooc8d t43;
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t44;
            ((uint8_t *)&t44)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t44 + 1), t43);
            uint8_t t45 = ((((uint8_t *)&t44))[0] != 0);
            F.ret = t44;
            goto gm_fret;
        }
        gm_l5 = t41;
    }
    {
        void * t46 = gm_l5;
        if ((uintptr_t)t46 % 8 != 0) gm_trap_misaligned((uintptr_t)t46, 8, gm_srcbsc23is03ttm, 89, 14);
        void * t47 = t46;
        void * t48 = ((uint8_t *)t47);
        uint64_t t49 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t50 = gm_l2;
        uint64_t t51 = (uintptr_t)t50;
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t48 + t49 * 64)));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(t51 - t49));
        gm_l6 = t52;
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t54 = gm_l2;
            gm_l7 = t54;
        }
        {
            for (;;) {
                uint64_t t56 = gm_l8;
                uint64_t t57 = gm_l7;
                uint8_t t58 = (t56 < t57);
                if (!t58) goto lb55;
                {
                    uint64_t t61 = gm_l8;
                    uint64_t t64 = GM_LD(uint64_t, ((uint8_t *)&t61));
                    gm_l9 = t64;
                    {
                        uint64_t t66 = gm_l9;
                        uint64_t t67 = (uintptr_t)t66;
                        uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_srcbsc23is03ttm, 90, 21);
                        GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t67 * 64) + 0), ((uint8_t)0ULL));
                    }
                    goto me62;
mn63:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 90, 5);
me62:;
                }
lc55:;
                {
                    uint64_t t70 = gm_l8;
                    uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                    uint64_t t73 = r71;
                    gm_l8 = t73;
                }
            }
lb55:;
        }
    }
    {
        void * t74 = gm_l0;
        gm_b16_8 t75 = gm_l6;
        GM_ST(gm_b16_8, (((uint8_t *)t74) + 0), t75);
    }
    {
        void * t76 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t76) + 16), ((uint64_t)0ULL));
    }
    {
        void * t77 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t77) + 24), ((uint64_t)0ULL));
    }
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l10 = t79;
        }
        {
            for (;;) {
                uint64_t t81 = gm_l11;
                uint64_t t82 = gm_l10;
                uint8_t t83 = (t81 < t82);
                if (!t83) goto lb80;
                {
                    uint64_t t86 = gm_l11;
                    uint64_t t89 = GM_LD(uint64_t, ((uint8_t *)&t86));
                    gm_l12 = t89;
                    {
                        uint64_t t92 = gm_l12;
                        uint64_t t93 = (uintptr_t)t92;
                        uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t93 >= (uintptr_t)(t94)) gm_trap_index(t93, t94, gm_srcbsc23is03ttm, 95, 12);
                        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t93 * 64) + 0));
                        uint8_t t96 = ((t95 == ((uint8_t)1ULL)));
                        if (!t96) goto ie91;
                        {
                            void * t98 = gm_l0;
                            uint64_t t99 = gm_l12;
                            uint64_t t100 = (uintptr_t)t99;
                            uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t100 >= (uintptr_t)(t101)) gm_trap_index(t100, t101, gm_srcbsc23is03ttm, 95, 49);
                            void * t102 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t100 * 64) + 8);
                            uint64_t t103 = gm_l12;
                            uint64_t t104 = (uintptr_t)t103;
                            uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcbsc23is03ttm, 95, 62);
                            void * t106 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 64) + 24);
                            gm_f_insert_new_0he56fl(t98, t102, t106);
                        }
                        goto ix91;
ie91:;
ix91:;
                    }
                    goto me87;
mn88:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 94, 5);
me87:;
                }
lc80:;
                {
                    uint64_t t108 = gm_l11;
                    uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                    uint64_t t111 = r109;
                    gm_l11 = t111;
                }
            }
lb80:;
        }
    }
    {
        uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t114 = (t113 > ((uint64_t)0ULL));
        if (!t114) goto ie112;
        {
            void * t116 = gm_l0;
            void * t117 = GM_LD(void *, ((((uint8_t *)t116) + 40) + 8));
            void * t118 = GM_LD(void *, (((uint8_t *)t117) + 16));
            void * t119 = gm_l0;
            void * t120 = GM_LD(void *, ((((uint8_t *)t119) + 40) + 0));
            void * t121 = GM_LD(void *, ((uint8_t *)&gm_l1));
            void * t122 = t121;
            uint64_t t123 = gm_l3;
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r125; bool o126 = __builtin_mul_overflow(t123, t124, &r125);
            if (o126) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 98, 60);
            uint64_t t127 = r125;
            uint64_t t128 = gm_l4;
            void * t129 = t118;
            gm_b0_1 t130;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t129))(t120, t122, t127, t128);
        }
        goto ix112;
ie112:;
ix112:;
    }
    {
        gm_b0_1 t131;
        gm_tftb1odc739t5 t132;
        GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
        ((uint8_t *)&t132)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t132 + 1), t131);
        uint8_t t133 = ((((uint8_t *)&t132))[0] != 0);
        F.ret = t132;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_ai5cphs(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_grow_ai5cphs F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* old */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* size */
    uint64_t gm_l4; /* align */
    void * gm_l5; /* raw */
    gm_b16_8 gm_l6; /* slots */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 810u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        gm_l1 = t3;
    }
    {
        uint64_t t4;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie5;
        uint64_t t9 = ((uint64_t)8ULL);
        t4 = t9;
        goto ix5;
ie5:;
        uint64_t t10;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 16));
        uint64_t r14; bool o15 = __builtin_mul_overflow(t13, ((uint64_t)2ULL), &r14);
        if (o15) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 85, 40);
        uint64_t t16 = r14;
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t18 = (t16 <= t17);
        if (!t18) goto ie11;
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t21 = t20;
        t10 = t21;
        goto ix11;
ie11:;
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r24; bool o25 = __builtin_mul_overflow(t23, ((uint64_t)2ULL), &r24);
        if (o25) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 85, 82);
        uint64_t t26 = r24;
        uint64_t t27 = t26;
        t10 = t27;
ix11:;
        t4 = t10;
ix5:;
        gm_l2 = t4;
    }
    {
        gm_l3 = ((uint64_t)32ULL);
    }
    {
        gm_l4 = ((uint64_t)8ULL);
    }
    {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, ((((uint8_t *)t28) + 40) + 8));
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, ((((uint8_t *)t31) + 40) + 0));
        uint64_t t33 = gm_l3;
        uint64_t t34 = gm_l2;
        uint64_t r35; bool o36 = __builtin_mul_overflow(t33, t34, &r35);
        if (o36) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 88, 43);
        uint64_t t37 = r35;
        uint64_t t38 = gm_l4;
        void * t39 = t30;
        void * t40;
        t40 = ((void * (*)(void *, uint64_t, uint64_t))GM_PTR_UNTAG(t39))(t32, t37, t38);
        void * t41;
        if ((GM_LD(void *, ((uint8_t *)&t40)) != 0)) {
            void * t42 = GM_LD(void *, (((uint8_t *)&t40) + 0));
            t41 = t42;
        } else {
            gm_t9qkh0rsooc8d t43;
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t44;
            ((uint8_t *)&t44)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t44 + 1), t43);
            uint8_t t45 = ((((uint8_t *)&t44))[0] != 0);
            F.ret = t44;
            goto gm_fret;
        }
        gm_l5 = t41;
    }
    {
        void * t46 = gm_l5;
        if ((uintptr_t)t46 % 8 != 0) gm_trap_misaligned((uintptr_t)t46, 8, gm_srcbsc23is03ttm, 89, 14);
        void * t47 = t46;
        void * t48 = ((uint8_t *)t47);
        uint64_t t49 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t50 = gm_l2;
        uint64_t t51 = (uintptr_t)t50;
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t48 + t49 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(t51 - t49));
        gm_l6 = t52;
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t54 = gm_l2;
            gm_l7 = t54;
        }
        {
            for (;;) {
                uint64_t t56 = gm_l8;
                uint64_t t57 = gm_l7;
                uint8_t t58 = (t56 < t57);
                if (!t58) goto lb55;
                {
                    uint64_t t61 = gm_l8;
                    uint64_t t64 = GM_LD(uint64_t, ((uint8_t *)&t61));
                    gm_l9 = t64;
                    {
                        uint64_t t66 = gm_l9;
                        uint64_t t67 = (uintptr_t)t66;
                        uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_srcbsc23is03ttm, 90, 21);
                        GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t67 * 32) + 0), ((uint8_t)0ULL));
                    }
                    goto me62;
mn63:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 90, 5);
me62:;
                }
lc55:;
                {
                    uint64_t t70 = gm_l8;
                    uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                    uint64_t t73 = r71;
                    gm_l8 = t73;
                }
            }
lb55:;
        }
    }
    {
        void * t74 = gm_l0;
        gm_b16_8 t75 = gm_l6;
        GM_ST(gm_b16_8, (((uint8_t *)t74) + 0), t75);
    }
    {
        void * t76 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t76) + 16), ((uint64_t)0ULL));
    }
    {
        void * t77 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t77) + 24), ((uint64_t)0ULL));
    }
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l10 = t79;
        }
        {
            for (;;) {
                uint64_t t81 = gm_l11;
                uint64_t t82 = gm_l10;
                uint8_t t83 = (t81 < t82);
                if (!t83) goto lb80;
                {
                    uint64_t t86 = gm_l11;
                    uint64_t t89 = GM_LD(uint64_t, ((uint8_t *)&t86));
                    gm_l12 = t89;
                    {
                        uint64_t t92 = gm_l12;
                        uint64_t t93 = (uintptr_t)t92;
                        uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t93 >= (uintptr_t)(t94)) gm_trap_index(t93, t94, gm_srcbsc23is03ttm, 95, 12);
                        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t93 * 32) + 0));
                        uint8_t t96 = ((t95 == ((uint8_t)1ULL)));
                        if (!t96) goto ie91;
                        {
                            void * t98 = gm_l0;
                            uint64_t t99 = gm_l12;
                            uint64_t t100 = (uintptr_t)t99;
                            uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t100 >= (uintptr_t)(t101)) gm_trap_index(t100, t101, gm_srcbsc23is03ttm, 95, 49);
                            void * t102 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t100 * 32) + 8);
                            uint64_t t103 = gm_l12;
                            uint64_t t104 = (uintptr_t)t103;
                            uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcbsc23is03ttm, 95, 62);
                            void * t106 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 32) + 24);
                            gm_f_insert_new_l14losc(t98, t102, t106);
                        }
                        goto ix91;
ie91:;
ix91:;
                    }
                    goto me87;
mn88:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 94, 5);
me87:;
                }
lc80:;
                {
                    uint64_t t108 = gm_l11;
                    uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                    uint64_t t111 = r109;
                    gm_l11 = t111;
                }
            }
lb80:;
        }
    }
    {
        uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t114 = (t113 > ((uint64_t)0ULL));
        if (!t114) goto ie112;
        {
            void * t116 = gm_l0;
            void * t117 = GM_LD(void *, ((((uint8_t *)t116) + 40) + 8));
            void * t118 = GM_LD(void *, (((uint8_t *)t117) + 16));
            void * t119 = gm_l0;
            void * t120 = GM_LD(void *, ((((uint8_t *)t119) + 40) + 0));
            void * t121 = GM_LD(void *, ((uint8_t *)&gm_l1));
            void * t122 = t121;
            uint64_t t123 = gm_l3;
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r125; bool o126 = __builtin_mul_overflow(t123, t124, &r125);
            if (o126) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 98, 60);
            uint64_t t127 = r125;
            uint64_t t128 = gm_l4;
            void * t129 = t118;
            gm_b0_1 t130;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t129))(t120, t122, t127, t128);
        }
        goto ix112;
ie112:;
ix112:;
    }
    {
        gm_b0_1 t131;
        gm_tftb1odc739t5 t132;
        GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
        ((uint8_t *)&t132)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t132 + 1), t131);
        uint8_t t133 = ((((uint8_t *)&t132))[0] != 0);
        F.ret = t132;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_aqcfk8r(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_grow_aqcfk8r F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* old */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* size */
    uint64_t gm_l4; /* align */
    void * gm_l5; /* raw */
    gm_b16_8 gm_l6; /* slots */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 810u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        gm_l1 = t3;
    }
    {
        uint64_t t4;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie5;
        uint64_t t9 = ((uint64_t)8ULL);
        t4 = t9;
        goto ix5;
ie5:;
        uint64_t t10;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 16));
        uint64_t r14; bool o15 = __builtin_mul_overflow(t13, ((uint64_t)2ULL), &r14);
        if (o15) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 85, 40);
        uint64_t t16 = r14;
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t18 = (t16 <= t17);
        if (!t18) goto ie11;
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t21 = t20;
        t10 = t21;
        goto ix11;
ie11:;
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r24; bool o25 = __builtin_mul_overflow(t23, ((uint64_t)2ULL), &r24);
        if (o25) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 85, 82);
        uint64_t t26 = r24;
        uint64_t t27 = t26;
        t10 = t27;
ix11:;
        t4 = t10;
ix5:;
        gm_l2 = t4;
    }
    {
        gm_l3 = ((uint64_t)24ULL);
    }
    {
        gm_l4 = ((uint64_t)8ULL);
    }
    {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, ((((uint8_t *)t28) + 40) + 8));
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, ((((uint8_t *)t31) + 40) + 0));
        uint64_t t33 = gm_l3;
        uint64_t t34 = gm_l2;
        uint64_t r35; bool o36 = __builtin_mul_overflow(t33, t34, &r35);
        if (o36) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 88, 43);
        uint64_t t37 = r35;
        uint64_t t38 = gm_l4;
        void * t39 = t30;
        void * t40;
        t40 = ((void * (*)(void *, uint64_t, uint64_t))GM_PTR_UNTAG(t39))(t32, t37, t38);
        void * t41;
        if ((GM_LD(void *, ((uint8_t *)&t40)) != 0)) {
            void * t42 = GM_LD(void *, (((uint8_t *)&t40) + 0));
            t41 = t42;
        } else {
            gm_t9qkh0rsooc8d t43;
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t44;
            ((uint8_t *)&t44)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t44 + 1), t43);
            uint8_t t45 = ((((uint8_t *)&t44))[0] != 0);
            F.ret = t44;
            goto gm_fret;
        }
        gm_l5 = t41;
    }
    {
        void * t46 = gm_l5;
        if ((uintptr_t)t46 % 8 != 0) gm_trap_misaligned((uintptr_t)t46, 8, gm_srcbsc23is03ttm, 89, 14);
        void * t47 = t46;
        void * t48 = ((uint8_t *)t47);
        uint64_t t49 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t50 = gm_l2;
        uint64_t t51 = (uintptr_t)t50;
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t48 + t49 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(t51 - t49));
        gm_l6 = t52;
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t54 = gm_l2;
            gm_l7 = t54;
        }
        {
            for (;;) {
                uint64_t t56 = gm_l8;
                uint64_t t57 = gm_l7;
                uint8_t t58 = (t56 < t57);
                if (!t58) goto lb55;
                {
                    uint64_t t61 = gm_l8;
                    uint64_t t64 = GM_LD(uint64_t, ((uint8_t *)&t61));
                    gm_l9 = t64;
                    {
                        uint64_t t66 = gm_l9;
                        uint64_t t67 = (uintptr_t)t66;
                        uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_srcbsc23is03ttm, 90, 21);
                        GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t67 * 24) + 0), ((uint8_t)0ULL));
                    }
                    goto me62;
mn63:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 90, 5);
me62:;
                }
lc55:;
                {
                    uint64_t t70 = gm_l8;
                    uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                    uint64_t t73 = r71;
                    gm_l8 = t73;
                }
            }
lb55:;
        }
    }
    {
        void * t74 = gm_l0;
        gm_b16_8 t75 = gm_l6;
        GM_ST(gm_b16_8, (((uint8_t *)t74) + 0), t75);
    }
    {
        void * t76 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t76) + 16), ((uint64_t)0ULL));
    }
    {
        void * t77 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t77) + 24), ((uint64_t)0ULL));
    }
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l10 = t79;
        }
        {
            for (;;) {
                uint64_t t81 = gm_l11;
                uint64_t t82 = gm_l10;
                uint8_t t83 = (t81 < t82);
                if (!t83) goto lb80;
                {
                    uint64_t t86 = gm_l11;
                    uint64_t t89 = GM_LD(uint64_t, ((uint8_t *)&t86));
                    gm_l12 = t89;
                    {
                        uint64_t t92 = gm_l12;
                        uint64_t t93 = (uintptr_t)t92;
                        uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t93 >= (uintptr_t)(t94)) gm_trap_index(t93, t94, gm_srcbsc23is03ttm, 95, 12);
                        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t93 * 24) + 0));
                        uint8_t t96 = ((t95 == ((uint8_t)1ULL)));
                        if (!t96) goto ie91;
                        {
                            void * t98 = gm_l0;
                            uint64_t t99 = gm_l12;
                            uint64_t t100 = (uintptr_t)t99;
                            uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t100 >= (uintptr_t)(t101)) gm_trap_index(t100, t101, gm_srcbsc23is03ttm, 95, 49);
                            void * t102 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t100 * 24) + 8);
                            uint64_t t103 = gm_l12;
                            uint64_t t104 = (uintptr_t)t103;
                            uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcbsc23is03ttm, 95, 62);
                            void * t106 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 24) + 24);
                            gm_f_insert_new_t150iph(t98, t102, t106);
                        }
                        goto ix91;
ie91:;
ix91:;
                    }
                    goto me87;
mn88:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 94, 5);
me87:;
                }
lc80:;
                {
                    uint64_t t108 = gm_l11;
                    uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                    uint64_t t111 = r109;
                    gm_l11 = t111;
                }
            }
lb80:;
        }
    }
    {
        uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t114 = (t113 > ((uint64_t)0ULL));
        if (!t114) goto ie112;
        {
            void * t116 = gm_l0;
            void * t117 = GM_LD(void *, ((((uint8_t *)t116) + 40) + 8));
            void * t118 = GM_LD(void *, (((uint8_t *)t117) + 16));
            void * t119 = gm_l0;
            void * t120 = GM_LD(void *, ((((uint8_t *)t119) + 40) + 0));
            void * t121 = GM_LD(void *, ((uint8_t *)&gm_l1));
            void * t122 = t121;
            uint64_t t123 = gm_l3;
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r125; bool o126 = __builtin_mul_overflow(t123, t124, &r125);
            if (o126) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 98, 60);
            uint64_t t127 = r125;
            uint64_t t128 = gm_l4;
            void * t129 = t118;
            gm_b0_1 t130;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t129))(t120, t122, t127, t128);
        }
        goto ix112;
ie112:;
ix112:;
    }
    {
        gm_b0_1 t131;
        gm_tftb1odc739t5 t132;
        GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
        ((uint8_t *)&t132)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t132 + 1), t131);
        uint8_t t133 = ((((uint8_t *)&t132))[0] != 0);
        F.ret = t132;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_qo0e2hr(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_grow_qo0e2hr F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* old */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* size */
    uint64_t gm_l4; /* align */
    void * gm_l5; /* raw */
    gm_b16_8 gm_l6; /* slots */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 810u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        gm_l1 = t3;
    }
    {
        uint64_t t4;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie5;
        uint64_t t9 = ((uint64_t)8ULL);
        t4 = t9;
        goto ix5;
ie5:;
        uint64_t t10;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 16));
        uint64_t r14; bool o15 = __builtin_mul_overflow(t13, ((uint64_t)2ULL), &r14);
        if (o15) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 85, 40);
        uint64_t t16 = r14;
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t18 = (t16 <= t17);
        if (!t18) goto ie11;
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t21 = t20;
        t10 = t21;
        goto ix11;
ie11:;
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r24; bool o25 = __builtin_mul_overflow(t23, ((uint64_t)2ULL), &r24);
        if (o25) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 85, 82);
        uint64_t t26 = r24;
        uint64_t t27 = t26;
        t10 = t27;
ix11:;
        t4 = t10;
ix5:;
        gm_l2 = t4;
    }
    {
        gm_l3 = ((uint64_t)32ULL);
    }
    {
        gm_l4 = ((uint64_t)8ULL);
    }
    {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, ((((uint8_t *)t28) + 40) + 8));
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, ((((uint8_t *)t31) + 40) + 0));
        uint64_t t33 = gm_l3;
        uint64_t t34 = gm_l2;
        uint64_t r35; bool o36 = __builtin_mul_overflow(t33, t34, &r35);
        if (o36) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 88, 43);
        uint64_t t37 = r35;
        uint64_t t38 = gm_l4;
        void * t39 = t30;
        void * t40;
        t40 = ((void * (*)(void *, uint64_t, uint64_t))GM_PTR_UNTAG(t39))(t32, t37, t38);
        void * t41;
        if ((GM_LD(void *, ((uint8_t *)&t40)) != 0)) {
            void * t42 = GM_LD(void *, (((uint8_t *)&t40) + 0));
            t41 = t42;
        } else {
            gm_t9qkh0rsooc8d t43;
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t44;
            ((uint8_t *)&t44)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t44 + 1), t43);
            uint8_t t45 = ((((uint8_t *)&t44))[0] != 0);
            F.ret = t44;
            goto gm_fret;
        }
        gm_l5 = t41;
    }
    {
        void * t46 = gm_l5;
        if ((uintptr_t)t46 % 8 != 0) gm_trap_misaligned((uintptr_t)t46, 8, gm_srcbsc23is03ttm, 89, 14);
        void * t47 = t46;
        void * t48 = ((uint8_t *)t47);
        uint64_t t49 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t50 = gm_l2;
        uint64_t t51 = (uintptr_t)t50;
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t48 + t49 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(t51 - t49));
        gm_l6 = t52;
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t54 = gm_l2;
            gm_l7 = t54;
        }
        {
            for (;;) {
                uint64_t t56 = gm_l8;
                uint64_t t57 = gm_l7;
                uint8_t t58 = (t56 < t57);
                if (!t58) goto lb55;
                {
                    uint64_t t61 = gm_l8;
                    uint64_t t64 = GM_LD(uint64_t, ((uint8_t *)&t61));
                    gm_l9 = t64;
                    {
                        uint64_t t66 = gm_l9;
                        uint64_t t67 = (uintptr_t)t66;
                        uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_srcbsc23is03ttm, 90, 21);
                        GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t67 * 32) + 0), ((uint8_t)0ULL));
                    }
                    goto me62;
mn63:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 90, 5);
me62:;
                }
lc55:;
                {
                    uint64_t t70 = gm_l8;
                    uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                    uint64_t t73 = r71;
                    gm_l8 = t73;
                }
            }
lb55:;
        }
    }
    {
        void * t74 = gm_l0;
        gm_b16_8 t75 = gm_l6;
        GM_ST(gm_b16_8, (((uint8_t *)t74) + 0), t75);
    }
    {
        void * t76 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t76) + 16), ((uint64_t)0ULL));
    }
    {
        void * t77 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t77) + 24), ((uint64_t)0ULL));
    }
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l10 = t79;
        }
        {
            for (;;) {
                uint64_t t81 = gm_l11;
                uint64_t t82 = gm_l10;
                uint8_t t83 = (t81 < t82);
                if (!t83) goto lb80;
                {
                    uint64_t t86 = gm_l11;
                    uint64_t t89 = GM_LD(uint64_t, ((uint8_t *)&t86));
                    gm_l12 = t89;
                    {
                        uint64_t t92 = gm_l12;
                        uint64_t t93 = (uintptr_t)t92;
                        uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t93 >= (uintptr_t)(t94)) gm_trap_index(t93, t94, gm_srcbsc23is03ttm, 95, 12);
                        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t93 * 32) + 0));
                        uint8_t t96 = ((t95 == ((uint8_t)1ULL)));
                        if (!t96) goto ie91;
                        {
                            void * t98 = gm_l0;
                            uint64_t t99 = gm_l12;
                            uint64_t t100 = (uintptr_t)t99;
                            uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t100 >= (uintptr_t)(t101)) gm_trap_index(t100, t101, gm_srcbsc23is03ttm, 95, 49);
                            void * t102 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t100 * 32) + 8);
                            uint64_t t103 = gm_l12;
                            uint64_t t104 = (uintptr_t)t103;
                            uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcbsc23is03ttm, 95, 62);
                            void * t106 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 32) + 16);
                            gm_f_insert_new_8duufql(t98, t102, t106);
                        }
                        goto ix91;
ie91:;
ix91:;
                    }
                    goto me87;
mn88:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 94, 5);
me87:;
                }
lc80:;
                {
                    uint64_t t108 = gm_l11;
                    uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                    uint64_t t111 = r109;
                    gm_l11 = t111;
                }
            }
lb80:;
        }
    }
    {
        uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t114 = (t113 > ((uint64_t)0ULL));
        if (!t114) goto ie112;
        {
            void * t116 = gm_l0;
            void * t117 = GM_LD(void *, ((((uint8_t *)t116) + 40) + 8));
            void * t118 = GM_LD(void *, (((uint8_t *)t117) + 16));
            void * t119 = gm_l0;
            void * t120 = GM_LD(void *, ((((uint8_t *)t119) + 40) + 0));
            void * t121 = GM_LD(void *, ((uint8_t *)&gm_l1));
            void * t122 = t121;
            uint64_t t123 = gm_l3;
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r125; bool o126 = __builtin_mul_overflow(t123, t124, &r125);
            if (o126) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 98, 60);
            uint64_t t127 = r125;
            uint64_t t128 = gm_l4;
            void * t129 = t118;
            gm_b0_1 t130;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t129))(t120, t122, t127, t128);
        }
        goto ix112;
ie112:;
ix112:;
    }
    {
        gm_b0_1 t131;
        gm_tftb1odc739t5 t132;
        GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
        ((uint8_t *)&t132)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t132 + 1), t131);
        uint8_t t133 = ((((uint8_t *)&t132))[0] != 0);
        F.ret = t132;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_hash_map_a2hplai(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_hash_map_abi2bn7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_hash_map_apl3mkc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_hash_map_ql49boo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_insert_new_a0qulo8(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insert_new_a0qulo8 F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    void * gm_l2; /* v */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 356u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 32));
        uint64_t t7 = gm_f_key_hash_7bps17u(t4, t6);
        uint64_t t8 = (uint64_t)t7;
        uint64_t t9 = gm_l3;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 73, 52);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        gm_l4 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcbsc23is03ttm, 74, 11);
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t17) + 0)) + t16 * 24) + 0));
            uint8_t t20 = ((t19 == ((uint8_t)1ULL)));
            if (!t20) goto lb14;
            {
                uint64_t t23 = gm_l4;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 74, 43);
                uint64_t t26 = r24;
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 74, 53);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t26 & t30);
                gm_l4 = t31;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcbsc23is03ttm, 75, 8);
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)) + t34 * 24) + 0));
        uint8_t t38 = ((t37 == ((uint8_t)0ULL)));
        if (!t38) goto ie32;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l0;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 24));
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 75, 36);
            uint64_t t45 = r43;
            GM_ST(uint64_t, (((uint8_t *)t40) + 24), t45);
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint64_t t46 = gm_l4;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcbsc23is03ttm, 76, 5);
        memset((uint8_t *)&(*(gm_t856vjvr00kbi *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 24)) + 1, 0, 7);
        void * t50 = gm_l1;
        int64_t t51 = GM_LD(int64_t, ((uint8_t *)t50));
        void * t52 = gm_l2;
        gm_tm98ap4112hee t53 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)t52));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t856vjvr00kbi *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 24)) + 0, ((uint8_t)1ULL));
        GM_ST(int64_t, (uint8_t *)&(*(gm_t856vjvr00kbi *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 24)) + 8, t51);
        GM_ST(gm_tm98ap4112hee, (uint8_t *)&(*(gm_t856vjvr00kbi *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 24)) + 16, t53);
    }
    {
        void * t54 = gm_l0;
        void * t55 = gm_l0;
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 16));
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 77, 5);
        uint64_t t59 = r57;
        GM_ST(uint64_t, (((uint8_t *)t54) + 16), t59);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_insert_new_a2a8f5d(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insert_new_a2a8f5d F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    void * gm_l2; /* v */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 376u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 32));
        uint64_t t7 = gm_f_key_hash_4r9ue8o(t4, t6);
        uint64_t t8 = (uint64_t)t7;
        uint64_t t9 = gm_l3;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 73, 52);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        gm_l4 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcbsc23is03ttm, 74, 11);
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t17) + 0)) + t16 * 48) + 0));
            uint8_t t20 = ((t19 == ((uint8_t)1ULL)));
            if (!t20) goto lb14;
            {
                uint64_t t23 = gm_l4;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 74, 43);
                uint64_t t26 = r24;
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 74, 53);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t26 & t30);
                gm_l4 = t31;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcbsc23is03ttm, 75, 8);
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)) + t34 * 48) + 0));
        uint8_t t38 = ((t37 == ((uint8_t)0ULL)));
        if (!t38) goto ie32;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l0;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 24));
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 75, 36);
            uint64_t t45 = r43;
            GM_ST(uint64_t, (((uint8_t *)t40) + 24), t45);
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint64_t t46 = gm_l4;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcbsc23is03ttm, 76, 5);
        memset((uint8_t *)&(*(gm_to2fqbbntv85f *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 48)) + 1, 0, 7);
        memset((uint8_t *)&(*(gm_to2fqbbntv85f *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 48)) + 44, 0, 4);
        void * t50 = gm_l1;
        gm_tiilmfd16v3fc t51 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)t50));
        void * t52 = gm_l2;
        gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)t52));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to2fqbbntv85f *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 48)) + 0, ((uint8_t)1ULL));
        GM_ST(gm_tiilmfd16v3fc, (uint8_t *)&(*(gm_to2fqbbntv85f *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 48)) + 8, t51);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_to2fqbbntv85f *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 48)) + 32, t53);
    }
    {
        void * t54 = gm_l0;
        void * t55 = gm_l0;
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 16));
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 77, 5);
        uint64_t t59 = r57;
        GM_ST(uint64_t, (((uint8_t *)t54) + 16), t59);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_insert_new_a2n7fmc(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insert_new_a2n7fmc F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    void * gm_l2; /* v */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 364u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 32));
        uint64_t t7 = gm_f_key_hash_hebjkbg(t4, t6);
        uint64_t t8 = (uint64_t)t7;
        uint64_t t9 = gm_l3;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 73, 52);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        gm_l4 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcbsc23is03ttm, 74, 11);
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t17) + 0)) + t16 * 16) + 0));
            uint8_t t20 = ((t19 == ((uint8_t)1ULL)));
            if (!t20) goto lb14;
            {
                uint64_t t23 = gm_l4;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 74, 43);
                uint64_t t26 = r24;
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 74, 53);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t26 & t30);
                gm_l4 = t31;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcbsc23is03ttm, 75, 8);
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)) + t34 * 16) + 0));
        uint8_t t38 = ((t37 == ((uint8_t)0ULL)));
        if (!t38) goto ie32;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l0;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 24));
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 75, 36);
            uint64_t t45 = r43;
            GM_ST(uint64_t, (((uint8_t *)t40) + 24), t45);
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint64_t t46 = gm_l4;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcbsc23is03ttm, 76, 5);
        void * t50 = gm_l1;
        uint64_t t51 = GM_LD(uint64_t, ((uint8_t *)t50));
        void * t52 = gm_l2;
        gm_b0_1 t53 = GM_LD(gm_b0_1, ((uint8_t *)t52));
        gm_tphtgnn9vgkcv t54;
        GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)1ULL));
        GM_ST(uint64_t, ((uint8_t *)&t54 + 8), t51);
        GM_ST(gm_b0_1, ((uint8_t *)&t54 + 16), t53);
        GM_ST(gm_tphtgnn9vgkcv, ((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 16), t54);
    }
    {
        void * t55 = gm_l0;
        void * t56 = gm_l0;
        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t56) + 16));
        uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
        if (o59) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 77, 5);
        uint64_t t60 = r58;
        GM_ST(uint64_t, (((uint8_t *)t55) + 16), t60);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_insert_new_anb2gma(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insert_new_anb2gma F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    void * gm_l2; /* v */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 368u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 32));
        uint64_t t7 = gm_f_key_hash_ckite1s(t4, t6);
        uint64_t t8 = (uint64_t)t7;
        uint64_t t9 = gm_l3;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 73, 52);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        gm_l4 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcbsc23is03ttm, 74, 11);
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t17) + 0)) + t16 * 40) + 0));
            uint8_t t20 = ((t19 == ((uint8_t)1ULL)));
            if (!t20) goto lb14;
            {
                uint64_t t23 = gm_l4;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 74, 43);
                uint64_t t26 = r24;
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 74, 53);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t26 & t30);
                gm_l4 = t31;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcbsc23is03ttm, 75, 8);
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)) + t34 * 40) + 0));
        uint8_t t38 = ((t37 == ((uint8_t)0ULL)));
        if (!t38) goto ie32;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l0;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 24));
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 75, 36);
            uint64_t t45 = r43;
            GM_ST(uint64_t, (((uint8_t *)t40) + 24), t45);
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint64_t t46 = gm_l4;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcbsc23is03ttm, 76, 5);
        memset((uint8_t *)&(*(gm_ttahj15cedals *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 40)) + 1, 0, 7);
        memset((uint8_t *)&(*(gm_ttahj15cedals *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 40)) + 36, 0, 4);
        void * t50 = gm_l1;
        gm_tiilmfd16v3fc t51 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)t50));
        void * t52 = gm_l2;
        uint32_t t53 = GM_LD(uint32_t, ((uint8_t *)t52));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttahj15cedals *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 40)) + 0, ((uint8_t)1ULL));
        GM_ST(gm_tiilmfd16v3fc, (uint8_t *)&(*(gm_ttahj15cedals *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 40)) + 8, t51);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttahj15cedals *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 40)) + 32, t53);
    }
    {
        void * t54 = gm_l0;
        void * t55 = gm_l0;
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 16));
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 77, 5);
        uint64_t t59 = r57;
        GM_ST(uint64_t, (((uint8_t *)t54) + 16), t59);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_insert_new_q01idpd(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insert_new_q01idpd F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    void * gm_l2; /* v */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 360u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 32));
        uint64_t t7 = gm_f_key_hash_odgtuo9(t4, t6);
        uint64_t t8 = (uint64_t)t7;
        uint64_t t9 = gm_l3;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 73, 52);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        gm_l4 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcbsc23is03ttm, 74, 11);
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t17) + 0)) + t16 * 32) + 0));
            uint8_t t20 = ((t19 == ((uint8_t)1ULL)));
            if (!t20) goto lb14;
            {
                uint64_t t23 = gm_l4;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 74, 43);
                uint64_t t26 = r24;
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 74, 53);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t26 & t30);
                gm_l4 = t31;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcbsc23is03ttm, 75, 8);
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)) + t34 * 32) + 0));
        uint8_t t38 = ((t37 == ((uint8_t)0ULL)));
        if (!t38) goto ie32;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l0;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 24));
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 75, 36);
            uint64_t t45 = r43;
            GM_ST(uint64_t, (((uint8_t *)t40) + 24), t45);
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint64_t t46 = gm_l4;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcbsc23is03ttm, 76, 5);
        memset((uint8_t *)&(*(gm_tap3g6eptfeor *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 32)) + 1, 0, 7);
        memset((uint8_t *)&(*(gm_tap3g6eptfeor *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 32)) + 28, 0, 4);
        void * t50 = gm_l1;
        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)t50));
        void * t52 = gm_l2;
        uint32_t t53 = GM_LD(uint32_t, ((uint8_t *)t52));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tap3g6eptfeor *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 32)) + 0, ((uint8_t)1ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tap3g6eptfeor *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 32)) + 8, t51);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tap3g6eptfeor *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 32)) + 24, t53);
    }
    {
        void * t54 = gm_l0;
        void * t55 = gm_l0;
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 16));
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 77, 5);
        uint64_t t59 = r57;
        GM_ST(uint64_t, (((uint8_t *)t54) + 16), t59);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_insert_new_q9jfo6k(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insert_new_q9jfo6k F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    void * gm_l2; /* v */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 360u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 32));
        uint64_t t7 = gm_f_key_hash_nkn0rq8(t4, t6);
        uint64_t t8 = (uint64_t)t7;
        uint64_t t9 = gm_l3;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 73, 52);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        gm_l4 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcbsc23is03ttm, 74, 11);
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t17) + 0)) + t16 * 24) + 0));
            uint8_t t20 = ((t19 == ((uint8_t)1ULL)));
            if (!t20) goto lb14;
            {
                uint64_t t23 = gm_l4;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 74, 43);
                uint64_t t26 = r24;
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 74, 53);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t26 & t30);
                gm_l4 = t31;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcbsc23is03ttm, 75, 8);
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)) + t34 * 24) + 0));
        uint8_t t38 = ((t37 == ((uint8_t)0ULL)));
        if (!t38) goto ie32;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l0;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 24));
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 75, 36);
            uint64_t t45 = r43;
            GM_ST(uint64_t, (((uint8_t *)t40) + 24), t45);
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint64_t t46 = gm_l4;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcbsc23is03ttm, 76, 5);
        memset((uint8_t *)&(*(gm_tqhauhm6mj3ud *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 24)) + 1, 0, 3);
        void * t50 = gm_l1;
        uint32_t t51 = GM_LD(uint32_t, ((uint8_t *)t50));
        void * t52 = gm_l2;
        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)t52));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqhauhm6mj3ud *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 24)) + 0, ((uint8_t)1ULL));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqhauhm6mj3ud *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 24)) + 4, t51);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tqhauhm6mj3ud *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 24)) + 8, t53);
    }
    {
        void * t54 = gm_l0;
        void * t55 = gm_l0;
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 16));
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 77, 5);
        uint64_t t59 = r57;
        GM_ST(uint64_t, (((uint8_t *)t54) + 16), t59);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_insert_new_qkout7s(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insert_new_qkout7s F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    void * gm_l2; /* v */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 357u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 32));
        uint64_t t7 = gm_f_key_hash_nkn0rq8(t4, t6);
        uint64_t t8 = (uint64_t)t7;
        uint64_t t9 = gm_l3;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 73, 52);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        gm_l4 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcbsc23is03ttm, 74, 11);
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t17) + 0)) + t16 * 12) + 0));
            uint8_t t20 = ((t19 == ((uint8_t)1ULL)));
            if (!t20) goto lb14;
            {
                uint64_t t23 = gm_l4;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 74, 43);
                uint64_t t26 = r24;
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 74, 53);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t26 & t30);
                gm_l4 = t31;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcbsc23is03ttm, 75, 8);
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)) + t34 * 12) + 0));
        uint8_t t38 = ((t37 == ((uint8_t)0ULL)));
        if (!t38) goto ie32;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l0;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 24));
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 75, 36);
            uint64_t t45 = r43;
            GM_ST(uint64_t, (((uint8_t *)t40) + 24), t45);
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint64_t t46 = gm_l4;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcbsc23is03ttm, 76, 5);
        void * t50 = gm_l1;
        uint32_t t51 = GM_LD(uint32_t, ((uint8_t *)t50));
        void * t52 = gm_l2;
        uint8_t t53 = GM_LD(uint8_t, ((uint8_t *)t52));
        gm_tfc64ef2cjub8 t54;
        GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
        GM_ST(uint8_t, (uint8_t *)&t54 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 10, 0);
        GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)1ULL));
        GM_ST(uint32_t, ((uint8_t *)&t54 + 4), t51);
        GM_ST(uint8_t, ((uint8_t *)&t54 + 8), t53);
        GM_ST(gm_tfc64ef2cjub8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 12), t54);
    }
    {
        void * t55 = gm_l0;
        void * t56 = gm_l0;
        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t56) + 16));
        uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
        if (o59) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 77, 5);
        uint64_t t60 = r58;
        GM_ST(uint64_t, (((uint8_t *)t55) + 16), t60);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) uint8_t gm_f_key_eq_agm4agv(void * a0, void * a1);
extern __attribute__((aligned(4))) uint8_t gm_f_key_eq_qasrbu4(void * a0, void * a1);
extern __attribute__((aligned(4))) uint64_t gm_f_key_hash_a3h40i4(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) uint64_t gm_f_key_hash_a8a5e8m(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) uint64_t gm_f_key_hash_at8t7r1(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) uint64_t gm_f_key_hash_qa443hf(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_keys_qc5bjlr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_keys_qc5bjlr F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 179u);
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
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l3 = t14;
                    {
                        uint64_t t17 = gm_l3;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = gm_l0;
                        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 0) + 8);
                        if (t18 >= (uintptr_t)(t20)) gm_trap_index(t18, t20, gm_srcbsc23is03ttm, 194, 12);
                        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 0)) + t18 * 32) + 0));
                        uint8_t t22 = ((t21 == ((uint8_t)1ULL)));
                        if (!t22) goto ie16;
                        {
                            uint64_t t24 = gm_l3;
                            uint64_t t25 = (uintptr_t)t24;
                            void * t26 = gm_l0;
                            uint64_t t27 = GM_LD(uintptr_t, (((uint8_t *)t26) + 0) + 8);
                            if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srcbsc23is03ttm, 194, 45);
                            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)t26) + 0)) + t25 * 32) + 8));
                            gm_node *pn29 = gm_find(cx, 993381376);
                            if (!pn29) gm_trap("no handler installed for effect `Yield`", gm_srcbsc23is03ttm, 194, 39);
                            gm_b0_1 t30;
                            t30 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn29->clauses[0])(pn29->env, pn29->hctx, pn29, t28);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix16;
ie16:;
ix16:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 193, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t32 = gm_l2;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l2 = t35;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_keys_qc5bjlr_s31qjug(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_keys_qc5bjlr F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 179u);
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
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l3 = t14;
                    {
                        uint64_t t17 = gm_l3;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = gm_l0;
                        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 0) + 8);
                        if (t18 >= (uintptr_t)(t20)) gm_trap_index(t18, t20, gm_srcbsc23is03ttm, 194, 12);
                        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 0)) + t18 * 32) + 0));
                        uint8_t t22 = ((t21 == ((uint8_t)1ULL)));
                        if (!t22) goto ie16;
                        {
                            uint64_t t24 = gm_l3;
                            uint64_t t25 = (uintptr_t)t24;
                            void * t26 = gm_l0;
                            uint64_t t27 = GM_LD(uintptr_t, (((uint8_t *)t26) + 0) + 8);
                            if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srcbsc23is03ttm, 194, 45);
                            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)t26) + 0)) + t25 * 32) + 8));
                            gm_b0_1 t29 = gm_f_for_body_0h3rgr6(cx->env, cx->hctx, cx, t28);
                        }
                        goto ix16;
ie16:;
ix16:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 193, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t31 = gm_l2;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l2 = t34;
                }
            }
lb5:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_a7l7vun(void * a0, gm_tcc1rq4n1avb2 a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_a7l7vun F;
    void * gm_l0; /* m */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    uint32_t gm_l2; /* v */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t5 = gm_f_find_ci1c6mk(t3, t4);
        if (!((((uint8_t *)&t5))[8] != 0)) goto ie2;
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&t5));
        gm_l3 = t6;
        {
            uint64_t t8 = gm_l3;
            uint64_t t9 = (uintptr_t)t8;
            void * t10 = gm_l0;
            uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 0) + 8);
            if (t9 >= (uintptr_t)(t11)) gm_trap_index(t9, t11, gm_srcbsc23is03ttm, 107, 9);
            uint32_t t12 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t10) + 0)) + t9 * 16) + 12), t12);
        }
        {
            gm_b0_1 t13;
            gm_tftb1odc739t5 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            ((uint8_t *)&t14)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t14 + 1), t13);
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 24));
        uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
        if (o20) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 110, 9);
        uint64_t t21 = r19;
        uint64_t r22; bool o23 = __builtin_mul_overflow(t21, ((uint64_t)4ULL), &r22);
        if (o23) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 8);
        uint64_t t24 = r22;
        void * t25 = gm_l0;
        uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 0) + 8);
        uint64_t r27; bool o28 = __builtin_mul_overflow(t26, ((uint64_t)3ULL), &r27);
        if (o28) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 27);
        uint64_t t29 = r27;
        uint8_t t30 = (t24 > t29);
        if (!t30) goto ie16;
        {
            void * t32 = gm_l0;
            gm_tftb1odc739t5 t33 = gm_f_grow_6cul2eh(t32);
            if (((((uint8_t *)&t33))[0] != 0)) {
                gm_t9qkh0rsooc8d t34 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t33) + 1));
                gm_tftb1odc739t5 t35;
                ((uint8_t *)&t35)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 1), t34);
                F.ret = t35;
                goto gm_fret;
            }
            gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t37 = gm_l0;
        void * t38 = (void *)((uint8_t *)&gm_l1);
        void * t39 = (void *)((uint8_t *)&gm_l2);
        gm_f_insert_new_p16jnkm(t37, t38, t39);
    }
    {
        gm_b0_1 t40;
        gm_tftb1odc739t5 t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
        ((uint8_t *)&t41)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t41 + 1), t40);
        uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
        F.ret = t41;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_amueool(void * a0, gm_cseahvg8v7p0vs a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_amueool F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* k */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 252u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t5 = gm_f_find_70gl1ke(t3, t4);
        if (!((((uint8_t *)&t5))[8] != 0)) goto ie2;
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&t5));
        gm_l3 = t6;
        {
            uint64_t t8 = gm_l3;
            uint64_t t9 = (uintptr_t)t8;
            void * t10 = gm_l0;
            uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 0) + 8);
            if (t9 >= (uintptr_t)(t11)) gm_trap_index(t9, t11, gm_srcbsc23is03ttm, 107, 9);
            gm_tl5vm0tcqvdvb t12 = F.l2;
            GM_ST(gm_tl5vm0tcqvdvb, (((uint8_t *)GM_LD(void *, (((uint8_t *)t10) + 0)) + t9 * 64) + 16), t12);
        }
        {
            gm_b0_1 t13;
            gm_tftb1odc739t5 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            ((uint8_t *)&t14)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t14 + 1), t13);
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 24));
        uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
        if (o20) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 110, 9);
        uint64_t t21 = r19;
        uint64_t r22; bool o23 = __builtin_mul_overflow(t21, ((uint64_t)4ULL), &r22);
        if (o23) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 8);
        uint64_t t24 = r22;
        void * t25 = gm_l0;
        uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 0) + 8);
        uint64_t r27; bool o28 = __builtin_mul_overflow(t26, ((uint64_t)3ULL), &r27);
        if (o28) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 27);
        uint64_t t29 = r27;
        uint8_t t30 = (t24 > t29);
        if (!t30) goto ie16;
        {
            void * t32 = gm_l0;
            gm_tftb1odc739t5 t33 = gm_f_grow_81srcpr(t32);
            if (((((uint8_t *)&t33))[0] != 0)) {
                gm_t9qkh0rsooc8d t34 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t33) + 1));
                gm_tftb1odc739t5 t35;
                ((uint8_t *)&t35)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 1), t34);
                F.ret = t35;
                goto gm_fret;
            }
            gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t37 = gm_l0;
        void * t38 = (void *)((uint8_t *)&gm_l1);
        void * t39 = (void *)((uint8_t *)&F.l2);
        gm_f_insert_new_kmsbdni(t37, t38, t39);
    }
    {
        gm_b0_1 t40;
        gm_tftb1odc739t5 t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
        ((uint8_t *)&t41)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t41 + 1), t40);
        uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
        F.ret = t41;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_amueool_c(void * a0, gm_cseahvg8v7p0vs a1, gm_tl5vm0tcqvdvb a2) { return gm_f_put_amueool(a0, a1, &a2); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qkbvor7(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_qkbvor7 F;
    void * gm_l0; /* m */
    int64_t gm_l1; /* k */
    gm_tm98ap4112hee gm_l2; /* v */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_tm98ap4112hee, a2);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t5 = gm_f_find_hjk709p(t3, t4);
        if (!((((uint8_t *)&t5))[8] != 0)) goto ie2;
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&t5));
        gm_l3 = t6;
        {
            uint64_t t8 = gm_l3;
            uint64_t t9 = (uintptr_t)t8;
            void * t10 = gm_l0;
            uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 0) + 8);
            if (t9 >= (uintptr_t)(t11)) gm_trap_index(t9, t11, gm_srcbsc23is03ttm, 107, 9);
            gm_tm98ap4112hee t12 = gm_l2;
            GM_ST(gm_tm98ap4112hee, (((uint8_t *)GM_LD(void *, (((uint8_t *)t10) + 0)) + t9 * 24) + 16), t12);
        }
        {
            gm_b0_1 t13;
            gm_tftb1odc739t5 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            ((uint8_t *)&t14)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t14 + 1), t13);
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 24));
        uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
        if (o20) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 110, 9);
        uint64_t t21 = r19;
        uint64_t r22; bool o23 = __builtin_mul_overflow(t21, ((uint64_t)4ULL), &r22);
        if (o23) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 8);
        uint64_t t24 = r22;
        void * t25 = gm_l0;
        uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 0) + 8);
        uint64_t r27; bool o28 = __builtin_mul_overflow(t26, ((uint64_t)3ULL), &r27);
        if (o28) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 27);
        uint64_t t29 = r27;
        uint8_t t30 = (t24 > t29);
        if (!t30) goto ie16;
        {
            void * t32 = gm_l0;
            gm_tftb1odc739t5 t33 = gm_f_grow_uav5st9(t32);
            if (((((uint8_t *)&t33))[0] != 0)) {
                gm_t9qkh0rsooc8d t34 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t33) + 1));
                gm_tftb1odc739t5 t35;
                ((uint8_t *)&t35)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 1), t34);
                F.ret = t35;
                goto gm_fret;
            }
            gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t37 = gm_l0;
        void * t38 = (void *)((uint8_t *)&gm_l1);
        void * t39 = (void *)((uint8_t *)&gm_l2);
        gm_f_insert_new_9tncde1(t37, t38, t39);
    }
    {
        gm_b0_1 t40;
        gm_tftb1odc739t5 t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
        ((uint8_t *)&t41)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t41 + 1), t40);
        uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
        F.ret = t41;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qoojoqv(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_qoojoqv F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* v */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t5 = gm_f_find_ovslcai(t3, t4);
        if (!((((uint8_t *)&t5))[8] != 0)) goto ie2;
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&t5));
        gm_l3 = t6;
        {
            uint64_t t8 = gm_l3;
            uint64_t t9 = (uintptr_t)t8;
            void * t10 = gm_l0;
            uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 0) + 8);
            if (t9 >= (uintptr_t)(t11)) gm_trap_index(t9, t11, gm_srcbsc23is03ttm, 107, 9);
            uint64_t t12 = gm_l2;
            GM_ST(uint64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t10) + 0)) + t9 * 32) + 24), t12);
        }
        {
            gm_b0_1 t13;
            gm_tftb1odc739t5 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            ((uint8_t *)&t14)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t14 + 1), t13);
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 24));
        uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
        if (o20) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 110, 9);
        uint64_t t21 = r19;
        uint64_t r22; bool o23 = __builtin_mul_overflow(t21, ((uint64_t)4ULL), &r22);
        if (o23) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 8);
        uint64_t t24 = r22;
        void * t25 = gm_l0;
        uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 0) + 8);
        uint64_t r27; bool o28 = __builtin_mul_overflow(t26, ((uint64_t)3ULL), &r27);
        if (o28) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 27);
        uint64_t t29 = r27;
        uint8_t t30 = (t24 > t29);
        if (!t30) goto ie16;
        {
            void * t32 = gm_l0;
            gm_tftb1odc739t5 t33 = gm_f_grow_ci6cs98(t32);
            if (((((uint8_t *)&t33))[0] != 0)) {
                gm_t9qkh0rsooc8d t34 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t33) + 1));
                gm_tftb1odc739t5 t35;
                ((uint8_t *)&t35)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 1), t34);
                F.ret = t35;
                goto gm_fret;
            }
            gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t37 = gm_l0;
        void * t38 = (void *)((uint8_t *)&gm_l1);
        void * t39 = (void *)((uint8_t *)&gm_l2);
        gm_f_insert_new_vbg72ug(t37, t38, t39);
    }
    {
        gm_b0_1 t40;
        gm_tftb1odc739t5 t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
        ((uint8_t *)&t41)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t41 + 1), t40);
        uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
        F.ret = t41;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qrf6f79(void * a0, gm_trlabqsjjp0ds a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_qrf6f79 F;
    void * gm_l0; /* m */
    gm_trlabqsjjp0ds gm_l1; /* k */
    uint32_t gm_l2; /* v */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t5 = gm_f_find_8gjlcrm(t3, t4);
        if (!((((uint8_t *)&t5))[8] != 0)) goto ie2;
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&t5));
        gm_l3 = t6;
        {
            uint64_t t8 = gm_l3;
            uint64_t t9 = (uintptr_t)t8;
            void * t10 = gm_l0;
            uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 0) + 8);
            if (t9 >= (uintptr_t)(t11)) gm_trap_index(t9, t11, gm_srcbsc23is03ttm, 107, 9);
            uint32_t t12 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t10) + 0)) + t9 * 24) + 20), t12);
        }
        {
            gm_b0_1 t13;
            gm_tftb1odc739t5 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            ((uint8_t *)&t14)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t14 + 1), t13);
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 24));
        uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
        if (o20) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 110, 9);
        uint64_t t21 = r19;
        uint64_t r22; bool o23 = __builtin_mul_overflow(t21, ((uint64_t)4ULL), &r22);
        if (o23) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 8);
        uint64_t t24 = r22;
        void * t25 = gm_l0;
        uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 0) + 8);
        uint64_t r27; bool o28 = __builtin_mul_overflow(t26, ((uint64_t)3ULL), &r27);
        if (o28) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 27);
        uint64_t t29 = r27;
        uint8_t t30 = (t24 > t29);
        if (!t30) goto ie16;
        {
            void * t32 = gm_l0;
            gm_tftb1odc739t5 t33 = gm_f_grow_kpihb7f(t32);
            if (((((uint8_t *)&t33))[0] != 0)) {
                gm_t9qkh0rsooc8d t34 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t33) + 1));
                gm_tftb1odc739t5 t35;
                ((uint8_t *)&t35)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 1), t34);
                F.ret = t35;
                goto gm_fret;
            }
            gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t37 = gm_l0;
        void * t38 = (void *)((uint8_t *)&gm_l1);
        void * t39 = (void *)((uint8_t *)&gm_l2);
        gm_f_insert_new_e2nhmvi(t37, t38, t39);
    }
    {
        gm_b0_1 t40;
        gm_tftb1odc739t5 t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
        ((uint8_t *)&t41)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t41 + 1), t40);
        uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
        F.ret = t41;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
