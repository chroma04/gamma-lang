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
   record at the top of the frame (x86-64, Clang on AArch64), and below them where it
   keeps it at the bottom (GCC on AArch64 without a stack protector): there the frame is
   above it already. The comparison with the frame `p` tells which, and folds away. */
#define GM_STACK_CHECK(p, n) do { \
    uintptr_t gm_fa = (uintptr_t)__builtin_frame_address(0); \
    if (gm_fa < gm_stack_limit + (gm_fa > (uintptr_t)(p) ? (uintptr_t)(n) + sizeof(*(p)) : 0)) gm_stack_overflow(); \
} while (0)
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
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; } gm_t2abnvcnt53ot;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_t2abnvcnt53ot) == 4 && _Alignof(gm_t2abnvcnt53ot) == 1 && offsetof(gm_t2abnvcnt53ot, f0) == 0 && offsetof(gm_t2abnvcnt53ot, f1) == 1 && offsetof(gm_t2abnvcnt53ot, f2) == 2 && offsetof(gm_t2abnvcnt53ot, f3) == 3, "layout of `(u8, u8, u8, u8)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_tf5srh4mp4b3i f0; uint8_t f1; uint8_t pe[7]; } gm_th3eueiipivq6;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_tf1bjg81u0glv f0; uint8_t f1; uint8_t pe[7]; } gm_t8sds1gdern44;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tftksr4op114e f0; uint8_t f1; uint8_t pe[7]; } gm_tbqli5v4up09m;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th3eueiipivq6) == 24 && _Alignof(gm_th3eueiipivq6) == 8 && offsetof(gm_th3eueiipivq6, f0) == 0 && offsetof(gm_th3eueiipivq6, f1) == 16, "layout of `(len: ?u64, is_const: bool)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t8sds1gdern44) == 32 && _Alignof(gm_t8sds1gdern44) == 8 && offsetof(gm_t8sds1gdern44, f0) == 0 && offsetof(gm_t8sds1gdern44, f1) == 24, "layout of `?(usize, string)`");
_Static_assert(sizeof(gm_tbqli5v4up09m) == 48 && _Alignof(gm_tbqli5v4up09m) == 8 && offsetof(gm_tbqli5v4up09m, f0) == 0 && offsetof(gm_tbqli5v4up09m, f1) == 40, "layout of `?(label: string, url: string, next: usize)`");
struct gm_fr_backtick_run_4nutone {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_cells_ud23es9 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_trqfvmjtt4sij l4; /* cur */
    gm_b16_8 ret;
};
struct gm_fr_closing_run_kj0embu {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_code_block_6uq0k78 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_code_span_vblredi {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_demote_headings_tf6iqtn {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_b16_8 ret;
};
struct gm_fr_emphasis_into_0e8guvs {
    gm_frame h;
    gm_tftksr4op114e l4; /* l */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_escape_into_fhlr21n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fence_hq2pe32 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* out */
    gm_b16_8 ret;
};
struct gm_fr_find_close_fapfs1c {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_heading_rm86g4d {
    gm_frame h;
    gm_t8sds1gdern44 ret;
};
struct gm_fr_inline_into_arphbtu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_is_indented_d6n8n8l {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_table_rule_9r0ibck {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_item_marker_kvo4318 {
    gm_frame h;
    gm_th3eueiipivq6 ret;
};
struct gm_fr_join_e7fm1oo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_lines_of_t8v251a {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_link_ihkl4vd {
    gm_frame h;
    gm_tbqli5v4up09m ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_pop_vr08sii {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_render_fmcurbp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_render_into_o6canda {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* code */
    gm_trqfvmjtt4sij l9; /* code */
    gm_trqfvmjtt4sij l10; /* quote */
    gm_trqfvmjtt4sij l11; /* para */
    gm_b0_1 ret;
};
struct gm_fr_render_list_cq70hce {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* body */
    uint64_t ret;
};
struct gm_fr_render_table_67oiva0 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_safe_url_bhj1jha {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_squash_2iiihih {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_starts_block_1sq7tuj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_strip_one_space_smv2rv8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_trim_ebi5drb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_trim_end_2059dot {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_trim_start_lj2dhj3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unwrap_lines_59i2isg {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_url_path_ul3c89a {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_ws_at_dchgqis {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ws_before_jst10le {
    gm_frame h;
    uint64_t ret;
};
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint64_t gm_f_backtick_run_4nutone(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_cells_ud23es9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_closing_run_kj0embu(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_code_block_6uq0k78(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_code_span_vblredi(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_demote_headings_tf6iqtn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_emphasis_into_0e8guvs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_eq_ignore_case_qqsqlkn(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_into_fhlr21n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fence_hq2pe32(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_close_fapfs1c(gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_heading_rm86g4d(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t8sds1gdern44 gm_f_heading_rm86g4d_c(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_inline_into_arphbtu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_indented_d6n8n8l(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_table_rule_9r0ibck(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_item_marker_kvo4318(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_th3eueiipivq6 gm_f_item_marker_kvo4318_c(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_e7fm1oo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_lines_of_t8v251a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_link_ihkl4vd(void *gm_out, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tbqli5v4up09m gm_f_link_ihkl4vd_c(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_render_fmcurbp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_into_o6canda(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) uint64_t gm_f_render_list_cq70hce(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint64_t gm_f_render_table_67oiva0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_safe_url_bhj1jha(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_squash_2iiihih(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_block_1sq7tuj(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_strip_one_space_smv2rv8(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_ebi5drb(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_2059dot(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_lj2dhj3(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_unwrap_lines_59i2isg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_url_path_ul3c89a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3bvpoha(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6a3aen6(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dqbiqk0(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fgbjr48(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_h3ti3fb(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_j9u0bpk(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) uint64_t gm_f_ws_at_dchgqis(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint64_t gm_f_ws_before_jst10le(gm_b16_8 a0, uint64_t a1);
extern uint8_t gm_cus4oh838pa[8] __attribute__((aligned(8)));
extern const uint8_t gm_s0c4i5le3mc2l[23];
extern const uint8_t gm_s0u0sleiibm5v[3];
extern const uint8_t gm_s1hspsqta2g4h[9];
extern const uint8_t gm_s2bjh3dbncrc8[3];
extern const uint8_t gm_s2r68j08i2lia[5];
extern const uint8_t gm_s2t6ld4n1pdgu[5];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s57l2tgcfr4se[5];
extern const uint8_t gm_s57qe8p5hq2v9[13];
extern const uint8_t gm_s6pceda0js0g8[6];
extern const uint8_t gm_s6u6c6o8c4lg0[5];
extern const uint8_t gm_s7eippa38tft8[7];
extern const uint8_t gm_s7i070auf0cel[6];
extern const uint8_t gm_s7un5bnoqg548[4];
extern const uint8_t gm_s8mnqic20r702[7];
extern const uint8_t gm_savlppb39i8g6[6];
extern const uint8_t gm_sbdlpmcdardns[7];
extern const uint8_t gm_sblomia8fb3mu[20];
extern const uint8_t gm_sc4daeflaltnv[3];
extern const uint8_t gm_sciqh24mne54a[4];
extern const uint8_t gm_scobqq9qv6nnk[6];
extern const uint8_t gm_sdogehgrilpeb[3];
extern const uint8_t gm_sel2d9vtaa0ba[19];
extern const uint8_t gm_senose3qe4tvp[3];
extern const uint8_t gm_sg344a2p7u6i6[6];
extern const uint8_t gm_sgankvlme129s[3];
extern const uint8_t gm_sgh9e8ka5t0qj[12];
extern const uint8_t gm_shb921rk4rkpo[5];
extern const uint8_t gm_shnk1stovqggp[5];
extern const uint8_t gm_si6fvebhspqjd[15];
extern const uint8_t gm_siselrhsqskvi[6];
extern const uint8_t gm_sj2pdhqdnnh86[3];
extern const uint8_t gm_sj6g27066vk8v[5];
extern const uint8_t gm_skf4n3tc7nif9[5];
extern const uint8_t gm_skh4rkm9k1elb[8];
extern const uint8_t gm_sld7tlsq8opg9[15];
extern const uint8_t gm_slopv2r8r4od2[5];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_sm9cv8e73t8v3[14];
extern const uint8_t gm_sm9lq3mvrfi15[7];
extern const uint8_t gm_smlcnjfj6t3mc[6];
extern const uint8_t gm_snk6krn32vlub[10];
extern const uint8_t gm_snmep73bl34j8[7];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_sr12ukrgmispo[10];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src8pvk8vd1qnr1[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ssnu9a3et1ilt[6];
extern const uint8_t gm_stq0db711o573[15];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) uint64_t gm_f_backtick_run_4nutone(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_backtick_run_4nutone F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l1;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            uint64_t t6 = gm_l1;
            uint64_t t7 = (uintptr_t)t6;
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_src8pvk8vd1qnr1, 409, 25);
            uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t7 * 1));
            uint8_t t10 = ((t9 == ((uint8_t)96ULL)));
            if (!t10) goto lb2;
            {
                uint64_t t13 = gm_l1;
                uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)1ULL), &r14);
                if (o15) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 409, 39);
                uint64_t t16 = r14;
                gm_l1 = t16;
            }
lc2:;
        }
lb2:;
    }
    {
        uint64_t t17 = gm_l1;
        uint8_t t18 = 0;
        F.ret = t17;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_indented_d6n8n8l(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_indented_d6n8n8l F;
    gm_b16_8 gm_l0; /* line */
    GM_STACK_CHECK(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_som0q53jqvjh3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    uint8_t t4 = gm_f_starts_with_6vvt01e(t2, t3);
    if (t4) {
        gm_b16_8 t5 = gm_l0;
        gm_b16_8 t6 = gm_f_trim_ebi5drb(t5);
        gm_b16_8 t7 = t6;
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&t7) + 8);
        uint8_t t9 = (t8 > ((uint64_t)0ULL));
        t1 = t9;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_e7fm1oo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_join_e7fm1oo F;
    gm_b16_8 gm_l0; /* parts */
    gm_b16_8 gm_l1; /* sep */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tabb710d9mq2e t3;
    gm_f_join_sfknljb((void *)&t3, (void *)NULL, cx, NULL, t1, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t4 = t3;
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_src8pvk8vd1qnr1, 121, 73);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
    gm_b16_8 t6 = t5;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_vr08sii F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* x */
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
            gm_th2lqbo77pvsc t8;
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
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 16));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_b16_8 t29 = gm_l2;
        gm_th2lqbo77pvsc t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[16] = 1;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_render_fmcurbp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_render_fmcurbp F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t4 = gm_f_render_into_o6canda((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t6 = t5;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        uint8_t t8 = 0;
        F.ret = t7;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_strip_one_space_smv2rv8(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_strip_one_space_smv2rv8 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s3p0rbs9hc3lg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(1));
    gm_th2lqbo77pvsc t3;
    gm_f_strip_prefix_b4949s8((void *)&t3, t1, t2);
    gm_th2lqbo77pvsc t4 = t3;
    gm_b16_8 t5;
    if (((((uint8_t *)&t4))[16] != 0)) {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
        t5 = t6;
    } else {
        gm_b16_8 t7 = gm_l0;
        t5 = t7;
    }
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_ebi5drb(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_ebi5drb F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_f_trim_start_lj2dhj3(t1);
    gm_b16_8 t3 = gm_f_trim_end_2059dot(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_backtick_run_4nutone(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_cells_ud23es9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_cells_ud23es9 F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* t */
    uint64_t gm_l2; /* skip */
    uint64_t gm_l5; /* i */
    uint8_t gm_l6; /* c */
    GM_STACK_CHECK(&F, 675u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_trim_ebi5drb(t2);
        gm_l1 = t3;
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t5 = gm_l2;
            uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t7 = (t5 < t6);
            if (!t7) goto lb4;
            uint64_t t8 = gm_l2;
            uint64_t t9 = (uintptr_t)t8;
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_src8pvk8vd1qnr1, 334, 28);
            uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t9 * 1));
            uint8_t t12 = ((t11 == ((uint8_t)124ULL)));
            if (!t12) goto lb4;
            {
                uint64_t t15 = gm_l2;
                uint64_t r16; bool o17 = __builtin_add_overflow(t15, ((uint64_t)1ULL), &r16);
                if (o17) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 334, 45);
                uint64_t t18 = r16;
                gm_l2 = t18;
            }
lc4:;
        }
lb4:;
    }
    {
        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t20 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t21 = gm_l2;
        uint64_t t22 = (uintptr_t)t21;
        if (t22 > (uintptr_t)t19 || (uintptr_t)t19 > (uintptr_t)t19) gm_trap_slice(t22, (uintptr_t)t19, t19, gm_src8pvk8vd1qnr1, 335, 9);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t20 + t22 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)((uintptr_t)t19 - t22));
        gm_l1 = t23;
    }
    {
        gm_b16_8 t25 = gm_l1;
        gm_b16_8 t26;
        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s2u5posfkbgnq));
        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(1));
        uint8_t t27 = gm_f_ends_with_biriu5l(t25, t26);
        if (!t27) goto ie24;
        {
            uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            void * t30 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
            uint64_t t31 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r33; bool o34 = __builtin_sub_overflow(t32, ((uint64_t)1ULL), &r33);
            if (o34) gm_trap("integer overflow in `-`", gm_src8pvk8vd1qnr1, 336, 36);
            uint64_t t35 = r33;
            uint64_t t36 = (uintptr_t)t35;
            if (t31 > t36 || t36 > (uintptr_t)t29) gm_trap_slice(t31, t36, t29, gm_src8pvk8vd1qnr1, 336, 31);
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(((uint8_t *)t30 + t31 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(t36 - t31));
            gm_l1 = t37;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        gm_f_list_epvducg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t39 = gm_l5;
            uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t41 = (t39 < t40);
            if (!t41) goto lb38;
            {
                uint64_t t44 = gm_l5;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_src8pvk8vd1qnr1, 341, 14);
                uint8_t t47 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t45 * 1));
                gm_l6 = t47;
            }
            {
                uint8_t t49 = gm_l6;
                uint8_t t50 = ((t49 == ((uint8_t)92ULL)));
                if (!t50) goto ie48;
                gm_b16_8 t51 = gm_l1;
                gm_b16_8 t52 = t51;
                void * t53 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t52)));
                uint64_t t54 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t52) + 8);
                uint64_t t55 = gm_l5;
                uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                if (o57) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 342, 32);
                uint64_t t58 = r56;
                uint64_t t59 = (uintptr_t)t58;
                gm_tqs5p4n2tq19v t60;
                if (t59 < t54) {
                uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)t53 + t59 * 1));
                gm_tqs5p4n2tq19v t62;
                GM_ST(uint8_t, ((uint8_t *)&t62), t61);
                ((uint8_t *)&t62)[1] = 1;
                    t60 = t62;
                } else {
                gm_tqs5p4n2tq19v t63;
                memset(&t63, 0, sizeof t63);
                    t60 = t63;
                }
                gm_tqs5p4n2tq19v t64;
                GM_ST(uint8_t, ((uint8_t *)&t64), ((uint8_t)124ULL));
                ((uint8_t *)&t64)[1] = 1;
                gm_tqs5p4n2tq19v t65 = t60;
                gm_tqs5p4n2tq19v t66 = t64;
                uint8_t t67 = (gm_eqb44cb3cf1n(((uint8_t *)&t65), ((uint8_t *)&t66)));
                if (!t67) goto ie48;
                {
                    void * t69 = (void *)((uint8_t *)&F.l4);
                    gm_tftb1odc739t5 t70 = gm_f_push_t83rg2m(t69, ((uint8_t)124ULL));
                    gm_tftb1odc739t5 t71 = t70;
                    if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_src8pvk8vd1qnr1, 343, 13);
                    gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                }
                {
                    uint64_t t73 = gm_l5;
                    uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)2ULL), &r74);
                    if (o75) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 344, 13);
                    uint64_t t76 = r74;
                    gm_l5 = t76;
                }
                goto ix48;
ie48:;
                {
                    uint8_t t79 = gm_l6;
                    uint8_t t80 = ((t79 == ((uint8_t)124ULL)));
                    if (!t80) goto ie78;
                    {
                        void * t82 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                        gm_b16_8 t84 = t83;
                        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                        gm_b16_8 t86 = gm_f_trim_ebi5drb(t85);
                        gm_tftb1odc739t5 t87 = gm_f_push_3ef746b(t82, t86);
                        gm_tftb1odc739t5 t88 = t87;
                        if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 1, gm_src8pvk8vd1qnr1, 347, 17);
                        gm_b0_1 t89 = GM_LD(gm_b0_1, (((uint8_t *)&t88) + 1));
                    }
                    {
                        gm_f_list_uvfu1k1((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l4)), (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll42;
                        }
                    }
                    goto ix78;
ie78:;
                    {
                        void * t91 = (void *)((uint8_t *)&F.l4);
                        uint8_t t92 = gm_l6;
                        gm_tftb1odc739t5 t93 = gm_f_push_t83rg2m(t91, t92);
                        gm_tftb1odc739t5 t94 = t93;
                        if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_src8pvk8vd1qnr1, 350, 17);
                        gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
                    }
ix78:;
                }
                {
                    uint64_t t96 = gm_l5;
                    uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                    if (o98) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 352, 13);
                    uint64_t t99 = r97;
                    gm_l5 = t99;
                }
ix48:;
            }
lc38:;
        }
gm_ll42:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb38; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc38; }
        }
        goto gm_lad;
lb38:;
    }
    {
        void * t100 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t102 = t101;
        gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
        gm_b16_8 t104 = gm_f_trim_ebi5drb(t103);
        gm_tftb1odc739t5 t105 = gm_f_push_3ef746b(t100, t104);
        gm_tftb1odc739t5 t106 = t105;
        if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 1, gm_src8pvk8vd1qnr1, 355, 5);
        gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 1));
    }
    {
        gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        uint8_t t109 = 0;
        F.ret = t108;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_closing_run_kj0embu(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_closing_run_kj0embu F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* off */
    uint64_t gm_l4; /* start */
    uint64_t gm_l5; /* len */
    GM_STACK_CHECK_SHALLOW(&F, 381u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
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
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t10 = gm_l2;
                uint64_t t11 = (uintptr_t)t10;
                if (t11 > (uintptr_t)t8 || (uintptr_t)t8 > (uintptr_t)t8) gm_trap_slice(t11, (uintptr_t)t8, t8, gm_src8pvk8vd1qnr1, 417, 16);
                gm_b16_8 t12;
                GM_ST(void *, (uint8_t *)&t12, (void *)(((uint8_t *)t9 + t11 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)((uintptr_t)t8 - t11));
                gm_tf5srh4mp4b3i t13 = gm_f_index_of_hdeg351(t12, ((uint8_t)96ULL));
                gm_tf5srh4mp4b3i t14 = t13;
                uint64_t t15;
                if (((((uint8_t *)&t14))[8] != 0)) {
                    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&t14) + 0));
                    t15 = t16;
                } else {
                    gm_tf5srh4mp4b3i t17;
                    memset(&t17, 0, sizeof t17);
                    uint8_t t18 = 0;
                    F.ret = t17;
                    goto gm_fret;
                }
                gm_l3 = t15;
            }
            {
                uint64_t t19 = gm_l2;
                uint64_t t20 = gm_l3;
                uint64_t r21; bool o22 = __builtin_add_overflow(t19, t20, &r21);
                if (o22) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 418, 18);
                uint64_t t23 = r21;
                gm_l4 = t23;
            }
            {
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t25 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t26 = gm_l4;
                uint64_t t27 = (uintptr_t)t26;
                if (t27 > (uintptr_t)t24 || (uintptr_t)t24 > (uintptr_t)t24) gm_trap_slice(t27, (uintptr_t)t24, t24, gm_src8pvk8vd1qnr1, 419, 29);
                gm_b16_8 t28;
                GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t25 + t27 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)((uintptr_t)t24 - t27));
                uint64_t t29 = gm_f_backtick_run_4nutone(t28);
                gm_l5 = t29;
            }
            {
                uint64_t t31 = gm_l5;
                uint64_t t32 = gm_l1;
                uint8_t t33 = ((t31 == t32));
                if (!t33) goto ie30;
                {
                    uint64_t t35 = gm_l4;
                    gm_tf5srh4mp4b3i t36;
                    GM_ST(uint8_t, (uint8_t *)&t36 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t36 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t36 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t36), t35);
                    ((uint8_t *)&t36)[8] = 1;
                    uint8_t t37 = 0;
                    F.ret = t36;
                    goto gm_fret;
                }
                goto ix30;
ie30:;
ix30:;
            }
            {
                uint64_t t38 = gm_l4;
                uint64_t t39 = gm_l5;
                uint64_t r40; bool o41 = __builtin_add_overflow(t38, t39, &r40);
                if (o41) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 421, 13);
                uint64_t t42 = r40;
                gm_l2 = t42;
            }
lc2:;
        }
lb2:;
    }
    {
        gm_tf5srh4mp4b3i t43;
        memset(&t43, 0, sizeof t43);
        uint8_t t44 = 0;
        F.ret = t43;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_code_block_6uq0k78(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_code_block_6uq0k78 F;
    gm_b16_8 gm_l0; /* code */
    void * gm_l1; /* out */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgh9e8ka5t0qj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_src8pvk8vd1qnr1, 239, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(1));
        gm_b16_8 t9 = gm_f_join_e7fm1oo((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t10 = gm_l1;
        gm_b0_1 t11 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l1;
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sld7tlsq8opg9));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(14));
        gm_tftb1odc739t5 t14 = gm_f_extend_0cs85gs(t12, t13);
        gm_tftb1odc739t5 t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_src8pvk8vd1qnr1, 241, 5);
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_code_span_vblredi(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_code_span_vblredi F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 308u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = (t3 > ((uint64_t)2ULL));
        if (!t4) goto ie2;
        gm_b16_8 t5 = gm_l0;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(1));
        uint8_t t7 = gm_f_starts_with_6vvt01e(t5, t6);
        if (!t7) goto ie2;
        gm_b16_8 t8 = gm_l0;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(1));
        uint8_t t10 = gm_f_ends_with_biriu5l(t8, t9);
        if (!t10) goto ie2;
        {
            {
                gm_b16_8 t13 = gm_l0;
                gm_l1 = t13;
            }
            {
                gm_l2 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t15 = gm_l2;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint8_t t17 = (t15 < t16);
                    if (!t17) goto lb14;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t t21 = (uintptr_t)t20;
                        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t21 * 1));
                        uint8_t t24 = t23;
                        uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        gm_l3 = t27;
                        {
                            uint8_t t30 = gm_l3;
                            uint8_t t31 = (!(t30 == ((uint8_t)32ULL)));
                            if (!t31) goto ie29;
                            {
                                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                void * t34 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                                uint64_t t35 = (uintptr_t)((uint64_t)1ULL);
                                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                uint64_t r37; bool o38 = __builtin_sub_overflow(t36, ((uint64_t)1ULL), &r37);
                                if (o38) gm_trap("integer overflow in `-`", gm_src8pvk8vd1qnr1, 431, 43);
                                uint64_t t39 = r37;
                                uint64_t t40 = (uintptr_t)t39;
                                if (t35 > t40 || t40 > (uintptr_t)t33) gm_trap_slice(t35, t40, t33, gm_src8pvk8vd1qnr1, 431, 38);
                                gm_b16_8 t41;
                                GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t34 + t35 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t35));
                                uint8_t t42 = 0;
                                F.ret = t41;
                                goto gm_fret;
                            }
                            goto ix29;
ie29:;
ix29:;
                        }
                        goto me25;
mn26:;
                        gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 430, 9);
me25:;
                    }
lc14:;
                    {
                        uint64_t t44 = gm_l2;
                        uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                        uint64_t t47 = r45;
                        gm_l2 = t47;
                    }
                }
lb14:;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t48 = gm_l0;
        uint8_t t49 = 0;
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_demote_headings_tf6iqtn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_demote_headings_tf6iqtn F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* by */
    uint8_t gm_l2; /* in_fence */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_b16_8 gm_l7; /* line */
    gm_b16_8 gm_l8; /* t */
    gm_tf1bjg81u0glv gm_l9; /* h */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    GM_STACK_CHECK(&F, 804u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint8_t)0);
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_b16_8 t4 = gm_f_lines_of_t8v251a((void *)NULL, cx, NULL, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t5 = t4;
            gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
            gm_l4 = t6;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l5;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    gm_tf1bjg81u0glv t13;
                    uint64_t t14 = gm_l5;
                    GM_ST(uint64_t, (uint8_t *)&t13 + 0, t14);
                    uint64_t t15 = gm_l5;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t16 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t13 + 8, t18);
                    gm_tf1bjg81u0glv t19 = t13;
                    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&t19) + 0));
                    gm_l6 = t22;
                    gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
                    gm_l7 = t23;
                    {
                        gm_b16_8 t25 = gm_l7;
                        gm_b16_8 t26 = gm_f_trim_start_lj2dhj3(t25);
                        gm_l8 = t26;
                    }
                    {
                        gm_b16_8 t28 = gm_l8;
                        gm_b16_8 t29;
                        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s7un5bnoqg548));
                        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(3));
                        uint8_t t30 = gm_f_starts_with_6vvt01e(t28, t29);
                        if (!t30) goto ie27;
                        {
                            uint8_t t32 = gm_l2;
                            uint8_t t33 = (uint8_t)!t32;
                            gm_l2 = t33;
                        }
                        goto ix27;
ie27:;
ix27:;
                    }
                    {
                        uint64_t t35 = gm_l6;
                        uint8_t t36 = (t35 > ((uint64_t)0ULL));
                        if (!t36) goto ie34;
                        {
                            void * t38 = (void *)((uint8_t *)&F.l3);
                            gm_tftb1odc739t5 t39 = gm_f_push_t83rg2m(t38, ((uint8_t)10ULL));
                            gm_tftb1odc739t5 t40 = t39;
                            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_src8pvk8vd1qnr1, 157, 20);
                            gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                        }
                        goto ix34;
ie34:;
ix34:;
                    }
                    {
                        gm_b16_8 t43 = gm_l8;
                        gm_t8sds1gdern44 t44;
                        gm_f_heading_rm86g4d((void *)&t44, t43);
                        gm_t8sds1gdern44 t45 = t44;
                        if (!((((uint8_t *)&t45))[24] != 0)) goto ie42;
                        gm_tf1bjg81u0glv t46 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)&t45));
                        gm_l9 = t46;
                        {
                            uint8_t t49 = gm_l2;
                            uint8_t t50 = (uint8_t)!t49;
                            if (!t50) goto ie48;
                            gm_b16_8 t51 = gm_l7;
                            uint8_t t52 = gm_f_is_indented_d6n8n8l(t51);
                            uint8_t t53 = (uint8_t)!t52;
                            if (!t53) goto ie48;
                            {
                                {
                                    gm_l11 = ((uint64_t)0ULL);
                                }
                                {
                                    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 0));
                                    uint64_t t57 = gm_l1;
                                    uint64_t r58; bool o59 = __builtin_add_overflow(t56, t57, &r58);
                                    if (o59) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 160, 30);
                                    uint64_t t60 = r58;
                                    uint64_t t61 = (t60 < ((uint64_t)6ULL) ? t60 : ((uint64_t)6ULL));
                                    gm_l10 = t61;
                                }
                                {
                                    for (;;) {
                                        uint64_t t63 = gm_l11;
                                        uint64_t t64 = gm_l10;
                                        uint8_t t65 = (t63 < t64);
                                        if (!t65) goto lb62;
                                        {
                                            uint64_t t68 = gm_l11;
                                            uint64_t t69 = t68;
                                            {
                                                void * t73 = (void *)((uint8_t *)&F.l3);
                                                gm_tftb1odc739t5 t74 = gm_f_push_t83rg2m(t73, ((uint8_t)35ULL));
                                                gm_tftb1odc739t5 t75 = t74;
                                                if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_src8pvk8vd1qnr1, 160, 53);
                                                gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
                                            }
                                            goto me70;
mn71:;
                                            gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 160, 17);
me70:;
                                        }
lc62:;
                                        {
                                            uint64_t t78 = gm_l11;
                                            uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                                            uint64_t t81 = r79;
                                            gm_l11 = t81;
                                        }
                                    }
lb62:;
                                }
                            }
                            {
                                void * t82 = (void *)((uint8_t *)&F.l3);
                                gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                                gm_tnpifv20irnph t84;
                                GM_ST(gm_b16_8, ((uint8_t *)&t84 + 0), t83);
                                gm_tftb1odc739t5 t85 = gm_f_write_dqbiqk0(t82, t84);
                                gm_tftb1odc739t5 t86 = t85;
                                if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_src8pvk8vd1qnr1, 161, 17);
                                gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
                            }
                            {
                                goto lc7;
                            }
                            goto ix48;
ie48:;
ix48:;
                        }
                        goto ix42;
ie42:;
ix42:;
                    }
                    {
                        void * t88 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t89 = gm_l7;
                        gm_tftb1odc739t5 t90 = gm_f_extend_0cs85gs(t88, t89);
                        gm_tftb1odc739t5 t91 = t90;
                        if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 1, gm_src8pvk8vd1qnr1, 165, 9);
                        gm_b0_1 t92 = GM_LD(gm_b0_1, (((uint8_t *)&t91) + 1));
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 154, 5);
me20:;
                }
lc7:;
                {
                    uint64_t t94 = gm_l5;
                    uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                    uint64_t t97 = r95;
                    gm_l5 = t97;
                }
            }
lb7:;
        }
    }
    {
        gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t99 = t98;
        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
        uint8_t t101 = 0;
        F.ret = t100;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_emphasis_into_0e8guvs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_emphasis_into_0e8guvs F;
    gm_b16_8 gm_l0; /* s */
    void * gm_l1; /* out */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* c */
    uint64_t gm_l5; /* n */
    uint8_t gm_l6; /* opens */
    uint8_t gm_l7; /* d */
    gm_tf5srh4mp4b3i gm_l8; /* close */
    uint64_t gm_l9; /* j */
    GM_STACK_CHECK(&F, 1333u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
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
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_src8pvk8vd1qnr1, 441, 14);
                uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
                gm_l3 = t11;
            }
            {
                uint8_t t13 = gm_l3;
                uint8_t t14 = ((t13 == ((uint8_t)91ULL)));
                if (!t14) goto ie12;
                {
                    gm_b16_8 t17 = gm_l0;
                    uint64_t t18 = gm_l2;
                    gm_tbqli5v4up09m t19;
                    gm_f_link_ihkl4vd((void *)&t19, t17, t18);
                    gm_tbqli5v4up09m t20 = t19;
                    if (!((((uint8_t *)&t20))[40] != 0)) goto ie16;
                    gm_tftksr4op114e t21 = GM_LD(gm_tftksr4op114e, ((uint8_t *)&t20));
                    F.l4 = t21;
                    {
                        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 16));
                        uint8_t t25 = gm_f_safe_url_bhj1jha(t24);
                        if (!t25) goto ie23;
                        {
                            void * t27 = gm_l1;
                            gm_b16_8 t28;
                            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_snk6krn32vlub));
                            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(9));
                            gm_tftb1odc739t5 t29 = gm_f_extend_0cs85gs(t27, t28);
                            gm_tftb1odc739t5 t30 = t29;
                            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_src8pvk8vd1qnr1, 445, 21);
                            gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                        }
                        {
                            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 16));
                            void * t33 = gm_l1;
                            gm_b0_1 t34 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t32, t33);
                            if (gm_unw.kind) {
                                goto gm_ll6;
                            }
                        }
                        {
                            void * t35 = gm_l1;
                            gm_b16_8 t36;
                            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s2bjh3dbncrc8));
                            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t37 = gm_f_extend_0cs85gs(t35, t36);
                            gm_tftb1odc739t5 t38 = t37;
                            if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_src8pvk8vd1qnr1, 447, 21);
                            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
                        }
                        {
                            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                            void * t41 = gm_l1;
                            gm_b0_1 t42 = gm_f_emphasis_into_0e8guvs((void *)NULL, cx, NULL, t40, t41);
                            if (gm_unw.kind) {
                                goto gm_ll6;
                            }
                        }
                        {
                            void * t43 = gm_l1;
                            gm_b16_8 t44;
                            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_skf4n3tc7nif9));
                            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(4));
                            gm_tftb1odc739t5 t45 = gm_f_extend_0cs85gs(t43, t44);
                            gm_tftb1odc739t5 t46 = t45;
                            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_src8pvk8vd1qnr1, 449, 21);
                            gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
                        }
                        goto ix23;
ie23:;
                        {
                            uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            void * t50 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                            uint64_t t51 = gm_l2;
                            uint64_t t52 = (uintptr_t)t51;
                            uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 32));
                            uint64_t t54 = (uintptr_t)t53;
                            if (t52 > t54 || t54 > (uintptr_t)t49) gm_trap_slice(t52, t54, t49, gm_src8pvk8vd1qnr1, 451, 33);
                            gm_b16_8 t55;
                            GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t50 + t52 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(t54 - t52));
                            void * t56 = gm_l1;
                            gm_b0_1 t57 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t55, t56);
                            if (gm_unw.kind) {
                                goto gm_ll6;
                            }
                        }
ix23:;
                    }
                    {
                        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 32));
                        gm_l2 = t58;
                    }
                    {
                        goto lc2;
                    }
                    goto ix16;
ie16:;
ix16:;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                uint8_t t60 = gm_l3;
                uint8_t t61 = ((t60 == ((uint8_t)42ULL)));
                if (!t61) goto ie59;
                {
                    gm_l5 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t64 = gm_l5;
                        uint8_t t65 = (t64 < ((uint64_t)3ULL));
                        if (!t65) goto lb63;
                        uint64_t t66 = gm_l2;
                        uint64_t t67 = gm_l5;
                        uint64_t r68; bool o69 = __builtin_add_overflow(t66, t67, &r68);
                        if (o69) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 459, 29);
                        uint64_t t70 = r68;
                        uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t72 = (t70 < t71);
                        if (!t72) goto lb63;
                        uint64_t t73 = gm_l2;
                        uint64_t t74 = gm_l5;
                        uint64_t r75; bool o76 = __builtin_add_overflow(t73, t74, &r75);
                        if (o76) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 459, 49);
                        uint64_t t77 = r75;
                        uint64_t t78 = (uintptr_t)t77;
                        uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_src8pvk8vd1qnr1, 459, 47);
                        uint8_t t80 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t78 * 1));
                        uint8_t t81 = ((t80 == ((uint8_t)42ULL)));
                        if (!t81) goto lb63;
                        {
                            uint64_t t84 = gm_l5;
                            uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                            if (o86) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 459, 65);
                            uint64_t t87 = r85;
                            gm_l5 = t87;
                        }
lc63:;
                    }
lb63:;
                }
                {
                    gm_l6 = ((uint8_t)0);
                }
                {
                    gm_b16_8 t89 = gm_l0;
                    gm_b16_8 t90 = t89;
                    void * t91 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t90)));
                    uint64_t t92 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t90) + 8);
                    uint64_t t93 = gm_l2;
                    uint64_t t94 = gm_l5;
                    uint64_t r95; bool o96 = __builtin_add_overflow(t93, t94, &r95);
                    if (o96) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 461, 27);
                    uint64_t t97 = r95;
                    uint64_t t98 = (uintptr_t)t97;
                    gm_tqs5p4n2tq19v t99;
                    if (t98 < t92) {
                    uint8_t t100 = GM_LD(uint8_t, ((uint8_t *)t91 + t98 * 1));
                    gm_tqs5p4n2tq19v t101;
                    GM_ST(uint8_t, ((uint8_t *)&t101), t100);
                    ((uint8_t *)&t101)[1] = 1;
                        t99 = t101;
                    } else {
                    gm_tqs5p4n2tq19v t102;
                    memset(&t102, 0, sizeof t102);
                        t99 = t102;
                    }
                    gm_tqs5p4n2tq19v t103 = t99;
                    if (!((((uint8_t *)&t103))[1] != 0)) goto ie88;
                    uint8_t t104 = GM_LD(uint8_t, ((uint8_t *)&t103));
                    gm_l7 = t104;
                    {
                        uint8_t t106 = 0;
                        gm_b16_8 t107 = gm_l0;
                        uint64_t t108 = gm_l2;
                        uint64_t t109 = gm_l5;
                        uint64_t r110; bool o111 = __builtin_add_overflow(t108, t109, &r110);
                        if (o111) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 461, 53);
                        uint64_t t112 = r110;
                        uint64_t t113 = gm_f_ws_at_dchgqis(t107, t112);
                        uint8_t t114 = ((t113 == ((uint64_t)0ULL)));
                        if (t114) {
                            uint8_t t115 = gm_l7;
                            uint8_t t116 = (!(t115 == ((uint8_t)42ULL)));
                            t106 = t116;
                        } else t106 = 0;
                        gm_l6 = t106;
                    }
                    goto ix88;
ie88:;
ix88:;
                }
                {
                    gm_tf5srh4mp4b3i t117;
                    uint8_t t119 = gm_l6;
                    if (!t119) goto ie118;
                    gm_b16_8 t121 = gm_l0;
                    uint64_t t122 = gm_l2;
                    uint64_t t123 = gm_l5;
                    uint64_t r124; bool o125 = __builtin_add_overflow(t122, t123, &r124);
                    if (o125) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 462, 54);
                    uint64_t t126 = r124;
                    uint64_t t127 = gm_l5;
                    gm_tf5srh4mp4b3i t128 = gm_f_find_close_fapfs1c(t121, t126, t127);
                    gm_tf5srh4mp4b3i t129 = t128;
                    t117 = t129;
                    goto ix118;
ie118:;
                    gm_tf5srh4mp4b3i t131;
                    memset(&t131, 0, sizeof t131);
                    gm_tf5srh4mp4b3i t132 = t131;
                    t117 = t132;
ix118:;
                    gm_l8 = t117;
                }
                {
                    if (!((((uint8_t *)&gm_l8))[8] != 0)) goto ie133;
                    uint64_t t134 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
                    gm_l9 = t134;
                    {
                        void * t136 = gm_l1;
                        uint64_t t137 = gm_l5;
                        uint64_t t138 = t137;
                        gm_b16_8 t139;
                        uint64_t t142 = GM_LD(uint64_t, ((uint8_t *)&t138));
                        if (!(t142 == ((uint64_t)1ULL))) goto mn141;
                        gm_b16_8 t143;
                        GM_ST(void *, (uint8_t *)&t143, (void *)(gm_sj6g27066vk8v));
                        GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(4));
                        t139 = t143;
                        goto me140;
mn141:;
                        uint64_t t145 = GM_LD(uint64_t, ((uint8_t *)&t138));
                        if (!(t145 == ((uint64_t)2ULL))) goto mn144;
                        gm_b16_8 t146;
                        GM_ST(void *, (uint8_t *)&t146, (void *)(gm_s1hspsqta2g4h));
                        GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(8));
                        t139 = t146;
                        goto me140;
mn144:;
                        gm_b16_8 t148;
                        GM_ST(void *, (uint8_t *)&t148, (void *)(gm_s57qe8p5hq2v9));
                        GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(12));
                        t139 = t148;
                        goto me140;
mn147:;
                        gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 464, 28);
me140:;
                        gm_tftb1odc739t5 t149 = gm_f_extend_0cs85gs(t136, t139);
                        gm_tftb1odc739t5 t150 = t149;
                        if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 1, gm_src8pvk8vd1qnr1, 464, 17);
                        gm_b0_1 t151 = GM_LD(gm_b0_1, (((uint8_t *)&t150) + 1));
                    }
                    {
                        uint64_t t152 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        void * t153 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                        uint64_t t154 = gm_l2;
                        uint64_t t155 = gm_l5;
                        uint64_t r156; bool o157 = __builtin_add_overflow(t154, t155, &r156);
                        if (o157) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 469, 33);
                        uint64_t t158 = r156;
                        uint64_t t159 = (uintptr_t)t158;
                        uint64_t t160 = gm_l9;
                        uint64_t t161 = (uintptr_t)t160;
                        if (t159 > t161 || t161 > (uintptr_t)t152) gm_trap_slice(t159, t161, t152, gm_src8pvk8vd1qnr1, 469, 31);
                        gm_b16_8 t162;
                        GM_ST(void *, (uint8_t *)&t162, (void *)(((uint8_t *)t153 + t159 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(t161 - t159));
                        void * t163 = gm_l1;
                        gm_b0_1 t164 = gm_f_emphasis_into_0e8guvs((void *)NULL, cx, NULL, t162, t163);
                        if (gm_unw.kind) {
                            goto gm_ll6;
                        }
                    }
                    {
                        void * t165 = gm_l1;
                        uint64_t t166 = gm_l5;
                        uint64_t t167 = t166;
                        gm_b16_8 t168;
                        uint64_t t171 = GM_LD(uint64_t, ((uint8_t *)&t167));
                        if (!(t171 == ((uint64_t)1ULL))) goto mn170;
                        gm_b16_8 t172;
                        GM_ST(void *, (uint8_t *)&t172, (void *)(gm_ssnu9a3et1ilt));
                        GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(5));
                        t168 = t172;
                        goto me169;
mn170:;
                        uint64_t t174 = GM_LD(uint64_t, ((uint8_t *)&t167));
                        if (!(t174 == ((uint64_t)2ULL))) goto mn173;
                        gm_b16_8 t175;
                        GM_ST(void *, (uint8_t *)&t175, (void *)(gm_sr12ukrgmispo));
                        GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(9));
                        t168 = t175;
                        goto me169;
mn173:;
                        gm_b16_8 t177;
                        GM_ST(void *, (uint8_t *)&t177, (void *)(gm_si6fvebhspqjd));
                        GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(14));
                        t168 = t177;
                        goto me169;
mn176:;
                        gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 470, 28);
me169:;
                        gm_tftb1odc739t5 t178 = gm_f_extend_0cs85gs(t165, t168);
                        gm_tftb1odc739t5 t179 = t178;
                        if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 1, gm_src8pvk8vd1qnr1, 470, 17);
                        gm_b0_1 t180 = GM_LD(gm_b0_1, (((uint8_t *)&t179) + 1));
                    }
                    {
                        uint64_t t181 = gm_l9;
                        uint64_t t182 = gm_l5;
                        uint64_t r183; bool o184 = __builtin_add_overflow(t181, t182, &r183);
                        if (o184) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 475, 21);
                        uint64_t t185 = r183;
                        gm_l2 = t185;
                    }
                    goto ix133;
ie133:;
                    {
                        void * t187 = gm_l1;
                        gm_tftb1odc739t5 t188 = gm_f_push_t83rg2m(t187, ((uint8_t)42ULL));
                        gm_tftb1odc739t5 t189 = t188;
                        if (((((uint8_t *)&t189))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t189) + 1, gm_src8pvk8vd1qnr1, 477, 17);
                        gm_b0_1 t190 = GM_LD(gm_b0_1, (((uint8_t *)&t189) + 1));
                    }
                    {
                        uint64_t t191 = gm_l2;
                        uint64_t r192; bool o193 = __builtin_add_overflow(t191, ((uint64_t)1ULL), &r192);
                        if (o193) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 478, 17);
                        uint64_t t194 = r192;
                        gm_l2 = t194;
                    }
ix133:;
                }
                {
                    goto lc2;
                }
                goto ix59;
ie59:;
ix59:;
            }
            {
                uint64_t t195 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t196 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t197 = gm_l2;
                uint64_t t198 = (uintptr_t)t197;
                uint64_t t199 = gm_l2;
                uint64_t r200; bool o201 = __builtin_add_overflow(t199, ((uint64_t)1ULL), &r200);
                if (o201) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 482, 26);
                uint64_t t202 = r200;
                uint64_t t203 = (uintptr_t)t202;
                if (t198 > t203 || t203 > (uintptr_t)t195) gm_trap_slice(t198, t203, t195, gm_src8pvk8vd1qnr1, 482, 21);
                gm_b16_8 t204;
                GM_ST(void *, (uint8_t *)&t204, (void *)(((uint8_t *)t196 + t198 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(t203 - t198));
                void * t205 = gm_l1;
                gm_b0_1 t206 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t204, t205);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
            }
            {
                uint64_t t207 = gm_l2;
                uint64_t r208; bool o209 = __builtin_add_overflow(t207, ((uint64_t)1ULL), &r208);
                if (o209) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 483, 9);
                uint64_t t210 = r208;
                gm_l2 = t210;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_into_fhlr21n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_escape_into_fhlr21n F;
    gm_b16_8 gm_l0; /* s */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 1));
                    uint8_t t14 = t13;
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t14));
                    gm_l4 = t17;
                    {
                        uint8_t t19 = gm_l4;
                        uint8_t t20 = t19;
                        uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t23 == ((uint8_t)38ULL))) goto mn22;
                        void * t24 = gm_l1;
                        gm_b16_8 t25;
                        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_smlcnjfj6t3mc));
                        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(5));
                        gm_tftb1odc739t5 t26 = gm_f_extend_0cs85gs(t24, t25);
                        gm_tftb1odc739t5 t27 = t26;
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_src8pvk8vd1qnr1, 15, 20);
                        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                        goto me21;
mn22:;
                        uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t30 == ((uint8_t)60ULL))) goto mn29;
                        void * t31 = gm_l1;
                        gm_b16_8 t32;
                        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s6u6c6o8c4lg0));
                        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t33 = gm_f_extend_0cs85gs(t31, t32);
                        gm_tftb1odc739t5 t34 = t33;
                        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_src8pvk8vd1qnr1, 16, 20);
                        gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
                        goto me21;
mn29:;
                        uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t37 == ((uint8_t)62ULL))) goto mn36;
                        void * t38 = gm_l1;
                        gm_b16_8 t39;
                        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s57l2tgcfr4se));
                        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t40 = gm_f_extend_0cs85gs(t38, t39);
                        gm_tftb1odc739t5 t41 = t40;
                        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_src8pvk8vd1qnr1, 17, 20);
                        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                        goto me21;
mn36:;
                        uint8_t t44 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t44 == ((uint8_t)34ULL))) goto mn43;
                        void * t45 = gm_l1;
                        gm_b16_8 t46;
                        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s8mnqic20r702));
                        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(6));
                        gm_tftb1odc739t5 t47 = gm_f_extend_0cs85gs(t45, t46);
                        gm_tftb1odc739t5 t48 = t47;
                        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_src8pvk8vd1qnr1, 18, 20);
                        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                        goto me21;
mn43:;
                        uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t51 == ((uint8_t)39ULL))) goto mn50;
                        void * t52 = gm_l1;
                        gm_b16_8 t53;
                        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_scobqq9qv6nnk));
                        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(5));
                        gm_tftb1odc739t5 t54 = gm_f_extend_0cs85gs(t52, t53);
                        gm_tftb1odc739t5 t55 = t54;
                        if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_src8pvk8vd1qnr1, 19, 21);
                        gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
                        goto me21;
mn50:;
                        void * t58 = gm_l1;
                        uint8_t t59 = gm_l4;
                        gm_tftb1odc739t5 t60 = gm_f_push_t83rg2m(t58, t59);
                        gm_tftb1odc739t5 t61 = t60;
                        if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_src8pvk8vd1qnr1, 20, 18);
                        gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
                        goto me21;
mn57:;
                        gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 14, 9);
me21:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 13, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t64 = gm_l3;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    uint64_t t67 = r65;
                    gm_l3 = t67;
                }
            }
lb4:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_fence_hq2pe32(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fence_hq2pe32 F;
    gm_b16_8 gm_l0; /* content */
    uint64_t gm_l1; /* longest */
    uint64_t gm_l2; /* run */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* c */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 381u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
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
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 1));
                    uint8_t t14 = t13;
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t14));
                    gm_l5 = t17;
                    {
                        uint64_t t19;
                        uint8_t t21 = gm_l5;
                        uint8_t t22 = ((t21 == ((uint8_t)96ULL)));
                        if (!t22) goto ie20;
                        uint64_t t24 = gm_l2;
                        uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                        if (o26) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 129, 29);
                        uint64_t t27 = r25;
                        uint64_t t28 = t27;
                        t19 = t28;
                        goto ix20;
ie20:;
                        uint64_t t30 = ((uint64_t)0ULL);
                        t19 = t30;
ix20:;
                        gm_l2 = t19;
                    }
                    {
                        uint64_t t31 = gm_l1;
                        uint64_t t32 = gm_l2;
                        uint64_t t33 = (t31 > t32 ? t31 : t32);
                        gm_l1 = t33;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 128, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t35 = gm_l4;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l4 = t38;
                }
            }
lb4:;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t40 = gm_l1;
            uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
            if (o42) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 133, 18);
            uint64_t t43 = r41;
            uint64_t t44 = (t43 > ((uint64_t)3ULL) ? t43 : ((uint64_t)3ULL));
            gm_l7 = t44;
        }
        {
            for (;;) {
                uint64_t t46 = gm_l8;
                uint64_t t47 = gm_l7;
                uint8_t t48 = (t46 < t47);
                if (!t48) goto lb45;
                {
                    uint64_t t51 = gm_l8;
                    uint64_t t52 = t51;
                    {
                        void * t56 = (void *)((uint8_t *)&F.l6);
                        gm_tftb1odc739t5 t57 = gm_f_push_t83rg2m(t56, ((uint8_t)96ULL));
                        gm_tftb1odc739t5 t58 = t57;
                        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_src8pvk8vd1qnr1, 133, 40);
                        gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
                    }
                    goto me53;
mn54:;
                    gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 133, 5);
me53:;
                }
lc45:;
                {
                    uint64_t t61 = gm_l8;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l8 = t64;
                }
            }
lb45:;
        }
    }
    {
        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t66 = t65;
        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_close_fapfs1c(gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_close_fapfs1c F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* from */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* j */
    uint8_t gm_l4; /* stars */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 431u);
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
            uint64_t t4 = gm_l3;
            uint64_t t5 = gm_l2;
            uint64_t r6; bool o7 = __builtin_add_overflow(t4, t5, &r6);
            if (o7) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 491, 11);
            uint64_t t8 = r6;
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t10 = (t8 <= t9);
            if (!t10) goto lb3;
            {
                gm_l4 = ((uint8_t)1);
            }
            {
                {
                    gm_l6 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t14 = gm_l2;
                    gm_l5 = t14;
                }
                {
                    for (;;) {
                        uint64_t t16 = gm_l6;
                        uint64_t t17 = gm_l5;
                        uint8_t t18 = (t16 < t17);
                        if (!t18) goto lb15;
                        {
                            uint64_t t21 = gm_l6;
                            uint64_t t22 = t21;
                            uint64_t t25 = GM_LD(uint64_t, ((uint8_t *)&t22));
                            gm_l7 = t25;
                            {
                                uint64_t t28 = gm_l3;
                                uint64_t t29 = gm_l7;
                                uint64_t r30; bool o31 = __builtin_add_overflow(t28, t29, &r30);
                                if (o31) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 494, 18);
                                uint64_t t32 = r30;
                                uint64_t t33 = (uintptr_t)t32;
                                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src8pvk8vd1qnr1, 494, 16);
                                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                                uint8_t t36 = (!(t35 == ((uint8_t)42ULL)));
                                if (!t36) goto ie27;
                                {
                                    gm_l4 = ((uint8_t)0);
                                }
                                goto ix27;
ie27:;
ix27:;
                            }
                            goto me23;
mn24:;
                            gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 493, 9);
me23:;
                        }
lc15:;
                        {
                            uint64_t t39 = gm_l6;
                            uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                            uint64_t t42 = r40;
                            gm_l6 = t42;
                        }
                    }
lb15:;
                }
            }
            {
                uint8_t t44 = gm_l4;
                if (!t44) goto ie43;
                gm_b16_8 t45 = gm_l0;
                uint64_t t46 = gm_l3;
                uint64_t t47 = gm_f_ws_before_jst10le(t45, t46);
                uint8_t t48 = ((t47 == ((uint64_t)0ULL)));
                if (!t48) goto ie43;
                gm_b16_8 t49 = gm_l0;
                gm_b16_8 t50 = t49;
                void * t51 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t50)));
                uint64_t t52 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t50) + 8);
                uint64_t t53 = gm_l3;
                uint64_t t54 = gm_l2;
                uint64_t r55; bool o56 = __builtin_add_overflow(t53, t54, &r55);
                if (o56) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 496, 53);
                uint64_t t57 = r55;
                uint64_t t58 = (uintptr_t)t57;
                gm_tqs5p4n2tq19v t59;
                if (t58 < t52) {
                uint8_t t60 = GM_LD(uint8_t, ((uint8_t *)t51 + t58 * 1));
                gm_tqs5p4n2tq19v t61;
                GM_ST(uint8_t, ((uint8_t *)&t61), t60);
                ((uint8_t *)&t61)[1] = 1;
                    t59 = t61;
                } else {
                gm_tqs5p4n2tq19v t62;
                memset(&t62, 0, sizeof t62);
                    t59 = t62;
                }
                gm_tqs5p4n2tq19v t63;
                GM_ST(uint8_t, ((uint8_t *)&t63), ((uint8_t)42ULL));
                ((uint8_t *)&t63)[1] = 1;
                gm_tqs5p4n2tq19v t64 = t59;
                gm_tqs5p4n2tq19v t65 = t63;
                uint8_t t66 = (!gm_eqb44cb3cf1n(((uint8_t *)&t64), ((uint8_t *)&t65)));
                if (!t66) goto ie43;
                {
                    uint64_t t68 = gm_l3;
                    gm_tf5srh4mp4b3i t69;
                    GM_ST(uint8_t, (uint8_t *)&t69 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t69 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t69 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t69), t68);
                    ((uint8_t *)&t69)[8] = 1;
                    uint8_t t70 = 0;
                    F.ret = t69;
                    goto gm_fret;
                }
                goto ix43;
ie43:;
ix43:;
            }
            {
                uint64_t t71 = gm_l3;
                uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                if (o73) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 497, 9);
                uint64_t t74 = r72;
                gm_l3 = t74;
            }
lc3:;
        }
lb3:;
    }
    {
        gm_tf5srh4mp4b3i t75;
        memset(&t75, 0, sizeof t75);
        uint8_t t76 = 0;
        F.ret = t75;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_heading_rm86g4d(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_heading_rm86g4d F;
    gm_b16_8 gm_l0; /* t */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* rest */
    GM_STACK_CHECK(&F, 425u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l1;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            uint64_t t6 = gm_l1;
            uint64_t t7 = (uintptr_t)t6;
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_src8pvk8vd1qnr1, 251, 25);
            uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t7 * 1));
            uint8_t t10 = ((t9 == ((uint8_t)35ULL)));
            if (!t10) goto lb2;
            {
                uint64_t t13 = gm_l1;
                uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)1ULL), &r14);
                if (o15) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 251, 39);
                uint64_t t16 = r14;
                gm_l1 = t16;
            }
lc2:;
        }
lb2:;
    }
    {
        uint8_t t18 = 0;
        uint64_t t19 = gm_l1;
        uint8_t t20 = (t19 < ((uint64_t)1ULL));
        if (!t20) {
            uint64_t t21 = gm_l1;
            uint8_t t22 = (t21 > ((uint64_t)6ULL));
            t18 = t22;
        } else t18 = 1;
        if (!t18) goto ie17;
        {
            gm_t8sds1gdern44 t24;
            memset(&t24, 0, sizeof t24);
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t27 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t28 = gm_l1;
        uint64_t t29 = (uintptr_t)t28;
        if (t29 > (uintptr_t)t26 || (uintptr_t)t26 > (uintptr_t)t26) gm_trap_slice(t29, (uintptr_t)t26, t26, gm_src8pvk8vd1qnr1, 253, 30);
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t27 + t29 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)((uintptr_t)t26 - t29));
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(1));
        gm_th2lqbo77pvsc t32;
        gm_f_strip_prefix_b4949s8((void *)&t32, t30, t31);
        gm_th2lqbo77pvsc t33 = t32;
        gm_b16_8 t34;
        if (((((uint8_t *)&t33))[16] != 0)) {
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 0));
            t34 = t35;
        } else {
            gm_t8sds1gdern44 t36;
            memset(&t36, 0, sizeof t36);
            uint8_t t37 = 0;
            F.ret = t36;
            goto gm_fret;
        }
        gm_l2 = t34;
    }
    {
        gm_tf1bjg81u0glv t38;
        uint64_t t39 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t38 + 0, t39);
        gm_b16_8 t40 = gm_l2;
        gm_b16_8 t41 = gm_f_trim_ebi5drb(t40);
        GM_ST(gm_b16_8, (uint8_t *)&t38 + 8, t41);
        gm_t8sds1gdern44 t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t42 + 28, 0);
        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t42), t38);
        ((uint8_t *)&t42)[24] = 1;
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t8sds1gdern44 gm_f_heading_rm86g4d_c(gm_b16_8 a0) { gm_t8sds1gdern44 gm_r; gm_f_heading_rm86g4d(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_inline_into_arphbtu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_inline_into_arphbtu F;
    gm_b16_8 gm_l0; /* s */
    void * gm_l1; /* out */
    uint64_t gm_l2; /* plain_from */
    uint64_t gm_l3; /* pos */
    uint64_t gm_l4; /* off */
    uint64_t gm_l5; /* start */
    uint64_t gm_l6; /* n */
    uint64_t gm_l7; /* after */
    uint64_t gm_l8; /* len */
    GM_STACK_CHECK(&F, 737u);
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
            uint64_t t3 = gm_l3;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t10 = gm_l3;
                uint64_t t11 = (uintptr_t)t10;
                if (t11 > (uintptr_t)t8 || (uintptr_t)t8 > (uintptr_t)t8) gm_trap_slice(t11, (uintptr_t)t8, t8, gm_src8pvk8vd1qnr1, 387, 16);
                gm_b16_8 t12;
                GM_ST(void *, (uint8_t *)&t12, (void *)(((uint8_t *)t9 + t11 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)((uintptr_t)t8 - t11));
                gm_tf5srh4mp4b3i t13 = gm_f_index_of_hdeg351(t12, ((uint8_t)96ULL));
                gm_tf5srh4mp4b3i t14 = t13;
                uint64_t t15;
                if (((((uint8_t *)&t14))[8] != 0)) {
                    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&t14) + 0));
                    t15 = t16;
                } else {
                    goto lb2;
                }
                gm_l4 = t15;
            }
            {
                uint64_t t17 = gm_l3;
                uint64_t t18 = gm_l4;
                uint64_t r19; bool o20 = __builtin_add_overflow(t17, t18, &r19);
                if (o20) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 388, 18);
                uint64_t t21 = r19;
                gm_l5 = t21;
            }
            {
                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t23 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t24 = gm_l5;
                uint64_t t25 = (uintptr_t)t24;
                if (t25 > (uintptr_t)t22 || (uintptr_t)t22 > (uintptr_t)t22) gm_trap_slice(t25, (uintptr_t)t22, t22, gm_src8pvk8vd1qnr1, 389, 27);
                gm_b16_8 t26;
                GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t23 + t25 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)((uintptr_t)t22 - t25));
                uint64_t t27 = gm_f_backtick_run_4nutone(t26);
                gm_l6 = t27;
            }
            {
                uint64_t t28 = gm_l5;
                uint64_t t29 = gm_l6;
                uint64_t r30; bool o31 = __builtin_add_overflow(t28, t29, &r30);
                if (o31) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 390, 18);
                uint64_t t32 = r30;
                gm_l7 = t32;
            }
            {
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t35 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t36 = gm_l7;
                uint64_t t37 = (uintptr_t)t36;
                if (t37 > (uintptr_t)t34 || (uintptr_t)t34 > (uintptr_t)t34) gm_trap_slice(t37, (uintptr_t)t34, t34, gm_src8pvk8vd1qnr1, 391, 31);
                gm_b16_8 t38;
                GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t35 + t37 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)((uintptr_t)t34 - t37));
                uint64_t t39 = gm_l6;
                gm_tf5srh4mp4b3i t40 = gm_f_closing_run_kj0embu(t38, t39);
                gm_tf5srh4mp4b3i t41 = t40;
                if (!((((uint8_t *)&t41))[8] != 0)) goto ie33;
                uint64_t t42 = GM_LD(uint64_t, ((uint8_t *)&t41));
                gm_l8 = t42;
                {
                    uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t45 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t46 = gm_l2;
                    uint64_t t47 = (uintptr_t)t46;
                    uint64_t t48 = gm_l5;
                    uint64_t t49 = (uintptr_t)t48;
                    if (t47 > t49 || t49 > (uintptr_t)t44) gm_trap_slice(t47, t49, t44, gm_src8pvk8vd1qnr1, 392, 27);
                    gm_b16_8 t50;
                    GM_ST(void *, (uint8_t *)&t50, (void *)(((uint8_t *)t45 + t47 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(t49 - t47));
                    void * t51 = gm_l1;
                    gm_b0_1 t52 = gm_f_emphasis_into_0e8guvs((void *)NULL, cx, NULL, t50, t51);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                }
                {
                    void * t53 = gm_l1;
                    gm_b16_8 t54;
                    GM_ST(void *, (uint8_t *)&t54, (void *)(gm_s7eippa38tft8));
                    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(6));
                    gm_tftb1odc739t5 t55 = gm_f_extend_0cs85gs(t53, t54);
                    gm_tftb1odc739t5 t56 = t55;
                    if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_src8pvk8vd1qnr1, 393, 13);
                    gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
                }
                {
                    uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t59 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t60 = gm_l7;
                    uint64_t t61 = (uintptr_t)t60;
                    uint64_t t62 = gm_l7;
                    uint64_t t63 = gm_l8;
                    uint64_t r64; bool o65 = __builtin_add_overflow(t62, t63, &r64);
                    if (o65) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 394, 44);
                    uint64_t t66 = r64;
                    uint64_t t67 = (uintptr_t)t66;
                    if (t61 > t67 || t67 > (uintptr_t)t58) gm_trap_slice(t61, t67, t58, gm_src8pvk8vd1qnr1, 394, 35);
                    gm_b16_8 t68;
                    GM_ST(void *, (uint8_t *)&t68, (void *)(((uint8_t *)t59 + t61 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(t67 - t61));
                    gm_b16_8 t69 = gm_f_code_span_vblredi(t68);
                    void * t70 = gm_l1;
                    gm_b0_1 t71 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t69, t70);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                }
                {
                    void * t72 = gm_l1;
                    gm_b16_8 t73;
                    GM_ST(void *, (uint8_t *)&t73, (void *)(gm_skh4rkm9k1elb));
                    GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(7));
                    gm_tftb1odc739t5 t74 = gm_f_extend_0cs85gs(t72, t73);
                    gm_tftb1odc739t5 t75 = t74;
                    if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_src8pvk8vd1qnr1, 395, 13);
                    gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
                }
                {
                    uint64_t t77 = gm_l7;
                    uint64_t t78 = gm_l8;
                    uint64_t r79; bool o80 = __builtin_add_overflow(t77, t78, &r79);
                    if (o80) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 396, 26);
                    uint64_t t81 = r79;
                    uint64_t t82 = gm_l6;
                    uint64_t r83; bool o84 = __builtin_add_overflow(t81, t82, &r83);
                    if (o84) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 396, 26);
                    uint64_t t85 = r83;
                    gm_l2 = t85;
                }
                {
                    uint64_t t86 = gm_l2;
                    gm_l3 = t86;
                }
                goto ix33;
ie33:;
                {
                    uint64_t t88 = gm_l7;
                    gm_l3 = t88;
                }
ix33:;
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
    {
        uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t90 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t91 = gm_l2;
        uint64_t t92 = (uintptr_t)t91;
        if (t92 > (uintptr_t)t89 || (uintptr_t)t89 > (uintptr_t)t89) gm_trap_slice(t92, (uintptr_t)t89, t89, gm_src8pvk8vd1qnr1, 403, 19);
        gm_b16_8 t93;
        GM_ST(void *, (uint8_t *)&t93, (void *)(((uint8_t *)t90 + t92 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)((uintptr_t)t89 - t92));
        void * t94 = gm_l1;
        gm_b0_1 t95 = gm_f_emphasis_into_0e8guvs((void *)NULL, cx, NULL, t93, t94);
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_indented_d6n8n8l(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_table_rule_9r0ibck(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_table_rule_9r0ibck F;
    gm_b16_8 gm_l0; /* l */
    gm_b16_8 gm_l1; /* t */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_trim_ebi5drb(t2);
        gm_l1 = t3;
    }
    {
        gm_b16_8 t5 = gm_l1;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s2u5posfkbgnq));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(1));
        uint8_t t7 = gm_f_starts_with_6vvt01e(t5, t6);
        uint8_t t8 = (uint8_t)!t7;
        if (!t8) goto ie4;
        {
            uint8_t t10 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        {
            gm_b16_8 t12 = gm_l1;
            gm_l2 = t12;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l3;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l3;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t20 * 1));
                    uint8_t t23 = t22;
                    uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)&t23));
                    gm_l4 = t26;
                    {
                        uint8_t t29 = gm_l4;
                        gm_t2abnvcnt53ot t30;
                        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)124ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t30 + 1), ((uint8_t)45ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t30 + 2), ((uint8_t)58ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t30 + 3), ((uint8_t)32ULL));
                        gm_t2abnvcnt53ot t31 = t30;
                        uint8_t t32 = 0;
                        uint8_t t33 = t29;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t31) + 0, ((uint8_t *)&t33))) t32 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t31) + 1, ((uint8_t *)&t33))) t32 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t31) + 2, ((uint8_t *)&t33))) t32 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t31) + 3, ((uint8_t *)&t33))) t32 = 1;
                        uint8_t t34 = (uint8_t)(!t32);
                        if (!t34) goto ie28;
                        {
                            uint8_t t36 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 269, 5);
me24:;
                }
lc13:;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
            }
lb13:;
        }
    }
    {
        uint8_t t42 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_item_marker_kvo4318(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_item_marker_kvo4318 F;
    gm_b16_8 gm_l0; /* t */
    uint64_t gm_l1; /* digits */
    uint8_t gm_l2; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 353u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = 0;
        gm_b16_8 t4 = gm_l0;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sc4daeflaltnv));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(2));
        uint8_t t6 = gm_f_starts_with_6vvt01e(t4, t5);
        if (!t6) {
            gm_b16_8 t7 = gm_l0;
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sj2pdhqdnnh86));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(2));
            uint8_t t9 = gm_f_starts_with_6vvt01e(t7, t8);
            t3 = t9;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_tf5srh4mp4b3i t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t11 + 0), ((uint64_t)2ULL));
            GM_ST(uint8_t, ((uint8_t *)&t11 + 8), ((uint8_t)0));
            gm_th3eueiipivq6 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 20, 0);
            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t12), t11);
            ((uint8_t *)&t12)[16] = 1;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t15 = gm_l1;
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t17 = (t15 < t16);
            if (!t17) goto lb14;
            {
                uint64_t t19 = gm_l1;
                uint64_t t20 = (uintptr_t)t19;
                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_src8pvk8vd1qnr1, 261, 30);
                uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t20 * 1));
                gm_l2 = t22;
            }
            uint8_t t23 = 0;
            uint8_t t24 = gm_l2;
            uint8_t t25 = (((uint8_t)48ULL) <= t24);
            if (t25) {
                uint8_t t26 = gm_l2;
                uint8_t t27 = (t26 <= ((uint8_t)57ULL));
                t23 = t27;
            } else t23 = 0;
            uint8_t t28 = t23;
            if (!t28) goto lb14;
            {
                uint64_t t31 = gm_l1;
                uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                if (o33) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 261, 55);
                uint64_t t34 = r32;
                gm_l1 = t34;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t36 = gm_l1;
        uint8_t t37 = (t36 > ((uint64_t)0ULL));
        if (!t37) goto ie35;
        uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t39 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t40 = gm_l1;
        uint64_t t41 = (uintptr_t)t40;
        if (t41 > (uintptr_t)t38 || (uintptr_t)t38 > (uintptr_t)t38) gm_trap_slice(t41, (uintptr_t)t38, t38, gm_src8pvk8vd1qnr1, 262, 23);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t39 + t41 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)((uintptr_t)t38 - t41));
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_senose3qe4tvp));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(2));
        uint8_t t44 = gm_f_starts_with_6vvt01e(t42, t43);
        if (!t44) goto ie35;
        {
            uint64_t t46 = gm_l1;
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)2ULL), &r47);
            if (o48) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 262, 68);
            uint64_t t49 = r47;
            gm_tf5srh4mp4b3i t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t50 + 0), t49);
            GM_ST(uint8_t, ((uint8_t *)&t50 + 8), ((uint8_t)1));
            gm_th3eueiipivq6 t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 20, 0);
            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t51), t50);
            ((uint8_t *)&t51)[16] = 1;
            uint8_t t52 = 0;
            F.ret = t51;
            goto gm_fret;
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        gm_th3eueiipivq6 t53;
        memset(&t53, 0, sizeof t53);
        uint8_t t54 = 0;
        F.ret = t53;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th3eueiipivq6 gm_f_item_marker_kvo4318_c(gm_b16_8 a0) { gm_th3eueiipivq6 gm_r; gm_f_item_marker_kvo4318(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) gm_b16_8 gm_f_join_e7fm1oo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_lines_of_t8v251a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_of_t8v251a F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* end */
    uint64_t gm_l4; /* line_end */
    GM_STACK_CHECK(&F, 357u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_epvducg((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
                gm_l3 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l3;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src8pvk8vd1qnr1, 32, 34);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l3;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 32, 58);
                        uint64_t t23 = r21;
                        gm_l3 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l3;
                gm_l4 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t28 = (t26 < t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l4;
                uint64_t t30 = gm_l2;
                uint8_t t31 = (t29 > t30);
                if (!t31) goto ie25;
                uint64_t t32 = gm_l4;
                uint64_t r33; bool o34 = __builtin_sub_overflow(t32, ((uint64_t)1ULL), &r33);
                if (o34) gm_trap("integer overflow in `-`", gm_src8pvk8vd1qnr1, 34, 57);
                uint64_t t35 = r33;
                uint64_t t36 = (uintptr_t)t35;
                uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t36 >= (uintptr_t)(t37)) gm_trap_index(t36, t37, gm_src8pvk8vd1qnr1, 34, 52);
                uint8_t t38 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t36 * 1));
                uint8_t t39 = ((t38 == ((uint8_t)13ULL)));
                if (!t39) goto ie25;
                {
                    uint64_t t41 = gm_l4;
                    uint64_t r42; bool o43 = __builtin_sub_overflow(t41, ((uint64_t)1ULL), &r42);
                    if (o43) gm_trap("integer overflow in `-`", gm_src8pvk8vd1qnr1, 34, 81);
                    uint64_t t44 = r42;
                    gm_l4 = t44;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                void * t45 = (void *)((uint8_t *)&F.l1);
                uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t47 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t48 = gm_l2;
                uint64_t t49 = (uintptr_t)t48;
                uint64_t t50 = gm_l4;
                uint64_t t51 = (uintptr_t)t50;
                if (t49 > t51 || t51 > (uintptr_t)t46) gm_trap_slice(t49, t51, t46, gm_src8pvk8vd1qnr1, 35, 18);
                gm_b16_8 t52;
                GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t47 + t49 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(t51 - t49));
                gm_tftb1odc739t5 t53 = gm_f_push_3ef746b(t45, t52);
                gm_tftb1odc739t5 t54 = t53;
                if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_src8pvk8vd1qnr1, 35, 9);
                gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
            }
            {
                uint64_t t56 = gm_l3;
                uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                if (o58) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 36, 17);
                uint64_t t59 = r57;
                gm_l2 = t59;
            }
lc2:;
        }
lb2:;
    }
    {
        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t61 = 0;
        F.ret = t60;
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
__attribute__((aligned(4))) void gm_f_link_ihkl4vd(void *gm_out, gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_link_ihkl4vd F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* close */
    uint64_t gm_l3; /* end */
    gm_b16_8 gm_l4; /* url */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 981u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t r3; bool o4 = __builtin_add_overflow(t2, ((uint64_t)1ULL), &r3);
        if (o4) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 504, 14);
        uint64_t t5 = r3;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t7 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t8 = gm_l1;
        uint64_t r9; bool o10 = __builtin_add_overflow(t8, ((uint64_t)1ULL), &r9);
        if (o10) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 504, 25);
        uint64_t t11 = r9;
        uint64_t t12 = (uintptr_t)t11;
        if (t12 > (uintptr_t)t6 || (uintptr_t)t6 > (uintptr_t)t6) gm_trap_slice(t12, (uintptr_t)t6, t6, gm_src8pvk8vd1qnr1, 504, 23);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t7 + t12 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)((uintptr_t)t6 - t12));
        gm_tf5srh4mp4b3i t14 = gm_f_index_of_hdeg351(t13, ((uint8_t)93ULL));
        gm_tf5srh4mp4b3i t15 = t14;
        uint64_t t16;
        if (((((uint8_t *)&t15))[8] != 0)) {
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
            t16 = t17;
        } else {
            gm_tbqli5v4up09m t18;
            memset(&t18, 0, sizeof t18);
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        uint64_t r20; bool o21 = __builtin_add_overflow(t5, t16, &r20);
        if (o21) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 504, 14);
        uint64_t t22 = r20;
        gm_l2 = t22;
    }
    {
        gm_b16_8 t24 = gm_l0;
        gm_b16_8 t25 = t24;
        void * t26 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t25)));
        uint64_t t27 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t25) + 8);
        uint64_t t28 = gm_l2;
        uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
        if (o30) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 505, 14);
        uint64_t t31 = r29;
        uint64_t t32 = (uintptr_t)t31;
        gm_tqs5p4n2tq19v t33;
        if (t32 < t27) {
        uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)t26 + t32 * 1));
        gm_tqs5p4n2tq19v t35;
        GM_ST(uint8_t, ((uint8_t *)&t35), t34);
        ((uint8_t *)&t35)[1] = 1;
            t33 = t35;
        } else {
        gm_tqs5p4n2tq19v t36;
        memset(&t36, 0, sizeof t36);
            t33 = t36;
        }
        gm_tqs5p4n2tq19v t37;
        GM_ST(uint8_t, ((uint8_t *)&t37), ((uint8_t)40ULL));
        ((uint8_t *)&t37)[1] = 1;
        gm_tqs5p4n2tq19v t38 = t33;
        gm_tqs5p4n2tq19v t39 = t37;
        uint8_t t40 = (!gm_eqb44cb3cf1n(((uint8_t *)&t38), ((uint8_t *)&t39)));
        if (!t40) goto ie23;
        {
            gm_tbqli5v4up09m t42;
            memset(&t42, 0, sizeof t42);
            uint8_t t43 = 0;
            F.ret = t42;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        uint64_t t44 = gm_l2;
        uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)2ULL), &r45);
        if (o46) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 506, 12);
        uint64_t t47 = r45;
        uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t49 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t50 = gm_l2;
        uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)2ULL), &r51);
        if (o52) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 506, 27);
        uint64_t t53 = r51;
        uint64_t t54 = (uintptr_t)t53;
        if (t54 > (uintptr_t)t48 || (uintptr_t)t48 > (uintptr_t)t48) gm_trap_slice(t54, (uintptr_t)t48, t48, gm_src8pvk8vd1qnr1, 506, 25);
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t49 + t54 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)((uintptr_t)t48 - t54));
        gm_tf5srh4mp4b3i t56 = gm_f_index_of_hdeg351(t55, ((uint8_t)41ULL));
        gm_tf5srh4mp4b3i t57 = t56;
        uint64_t t58;
        if (((((uint8_t *)&t57))[8] != 0)) {
            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&t57) + 0));
            t58 = t59;
        } else {
            gm_tbqli5v4up09m t60;
            memset(&t60, 0, sizeof t60);
            uint8_t t61 = 0;
            F.ret = t60;
            goto gm_fret;
        }
        uint64_t r62; bool o63 = __builtin_add_overflow(t47, t58, &r62);
        if (o63) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 506, 12);
        uint64_t t64 = r62;
        gm_l3 = t64;
    }
    {
        uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t66 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t67 = gm_l2;
        uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)2ULL), &r68);
        if (o69) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 507, 14);
        uint64_t t70 = r68;
        uint64_t t71 = (uintptr_t)t70;
        uint64_t t72 = gm_l3;
        uint64_t t73 = (uintptr_t)t72;
        if (t71 > t73 || t73 > (uintptr_t)t65) gm_trap_slice(t71, t73, t65, gm_src8pvk8vd1qnr1, 507, 12);
        gm_b16_8 t74;
        GM_ST(void *, (uint8_t *)&t74, (void *)(((uint8_t *)t66 + t71 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(t73 - t71));
        gm_l4 = t74;
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            gm_l5 = t76;
        }
        {
            for (;;) {
                uint64_t t78 = gm_l6;
                uint64_t t79 = gm_l5;
                uint8_t t80 = (t78 < t79);
                if (!t80) goto lb77;
                {
                    uint64_t t83 = gm_l6;
                    uint64_t t84 = t83;
                    uint64_t t87 = GM_LD(uint64_t, ((uint8_t *)&t84));
                    gm_l7 = t87;
                    {
                        gm_b16_8 t90 = gm_l4;
                        uint64_t t91 = gm_l7;
                        uint64_t t92 = gm_f_ws_at_dchgqis(t90, t91);
                        uint8_t t93 = (t92 > ((uint64_t)0ULL));
                        if (!t93) goto ie89;
                        {
                            gm_tbqli5v4up09m t95;
                            memset(&t95, 0, sizeof t95);
                            uint8_t t96 = 0;
                            F.ret = t95;
                            goto gm_fret;
                        }
                        goto ix89;
ie89:;
ix89:;
                    }
                    goto me85;
mn86:;
                    gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 508, 5);
me85:;
                }
lc77:;
                {
                    uint64_t t98 = gm_l6;
                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                    uint64_t t101 = r99;
                    gm_l6 = t101;
                }
            }
lb77:;
        }
    }
    {
        gm_tftksr4op114e t102;
        uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t104 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t105 = gm_l1;
        uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
        if (o107) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 511, 22);
        uint64_t t108 = r106;
        uint64_t t109 = (uintptr_t)t108;
        uint64_t t110 = gm_l2;
        uint64_t t111 = (uintptr_t)t110;
        if (t109 > t111 || t111 > (uintptr_t)t103) gm_trap_slice(t109, t111, t103, gm_src8pvk8vd1qnr1, 511, 20);
        gm_b16_8 t112;
        GM_ST(void *, (uint8_t *)&t112, (void *)(((uint8_t *)t104 + t109 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(t111 - t109));
        GM_ST(gm_b16_8, (uint8_t *)&t102 + 0, t112);
        gm_b16_8 t113 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t102 + 16, t113);
        uint64_t t114 = gm_l3;
        uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
        if (o116) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 511, 53);
        uint64_t t117 = r115;
        GM_ST(uint64_t, (uint8_t *)&t102 + 32, t117);
        gm_tbqli5v4up09m t118;
        GM_ST(uint8_t, (uint8_t *)&t118 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t118 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t118 + 44, 0);
        GM_ST(gm_tftksr4op114e, ((uint8_t *)&t118), t102);
        ((uint8_t *)&t118)[40] = 1;
        uint8_t t119 = 0;
        F.ret = t118;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tbqli5v4up09m gm_f_link_ihkl4vd_c(gm_b16_8 a0, uint64_t a1) { gm_tbqli5v4up09m gm_r; gm_f_link_ihkl4vd(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) gm_b16_8 gm_f_render_fmcurbp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_into_o6canda(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_render_into_o6canda F;
    gm_b16_8 gm_l0; /* text */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* lines */
    uint64_t gm_l3; /* i */
    gm_b16_8 gm_l4; /* line */
    gm_b16_8 gm_l5; /* t */
    gm_tf1bjg81u0glv gm_l7; /* h */
    uint64_t gm_l8; /* level */
    GM_STACK_CHECK(&F, 2456u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_lines_of_t8v251a((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t5 = gm_l3;
            uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t7 = (t5 < t6);
            if (!t7) goto lb4;
            {
                uint64_t t10 = gm_l3;
                uint64_t t11 = (uintptr_t)t10;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_src8pvk8vd1qnr1, 181, 17);
                gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 16));
                gm_l4 = t13;
            }
            {
                gm_b16_8 t14 = gm_l4;
                gm_b16_8 t15 = gm_f_trim_start_lj2dhj3(t14);
                gm_l5 = t15;
            }
            {
                gm_b16_8 t17 = gm_l4;
                gm_b16_8 t18 = gm_f_trim_ebi5drb(t17);
                gm_b16_8 t19 = t18;
                uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&t19) + 8);
                uint8_t t21 = ((t20 == ((uint64_t)0ULL)));
                if (!t21) goto ie16;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    if (o25) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 184, 13);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
                goto ix16;
ie16:;
                gm_b16_8 t28 = gm_l5;
                gm_b16_8 t29;
                GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s7un5bnoqg548));
                GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(3));
                uint8_t t30 = gm_f_starts_with_6vvt01e(t28, t29);
                if (!t30) goto ie27;
                {
                    uint64_t t32 = gm_l3;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    if (o34) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 186, 13);
                    uint64_t t35 = r33;
                    gm_l3 = t35;
                }
                {
                    gm_f_list_epvducg((void *)&F.l6, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                }
                {
                    for (;;) {
                        uint64_t t37 = gm_l3;
                        uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        uint8_t t39 = (t37 < t38);
                        if (!t39) goto lb36;
                        uint64_t t40 = gm_l3;
                        uint64_t t41 = (uintptr_t)t40;
                        uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_src8pvk8vd1qnr1, 188, 52);
                        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t41 * 16));
                        gm_b16_8 t44 = gm_f_trim_start_lj2dhj3(t43);
                        gm_b16_8 t45;
                        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s7un5bnoqg548));
                        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(3));
                        uint8_t t46 = gm_f_starts_with_6vvt01e(t44, t45);
                        uint8_t t47 = (uint8_t)!t46;
                        if (!t47) goto lb36;
                        {
                            void * t50 = (void *)((uint8_t *)&F.l6);
                            uint64_t t51 = gm_l3;
                            uint64_t t52 = (uintptr_t)t51;
                            uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t52 >= (uintptr_t)(t53)) gm_trap_index(t52, t53, gm_src8pvk8vd1qnr1, 189, 27);
                            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t52 * 16));
                            gm_tftb1odc739t5 t55 = gm_f_push_3ef746b(t50, t54);
                            gm_tftb1odc739t5 t56 = t55;
                            if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_src8pvk8vd1qnr1, 189, 17);
                            gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
                        }
                        {
                            uint64_t t58 = gm_l3;
                            uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                            if (o60) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 190, 17);
                            uint64_t t61 = r59;
                            gm_l3 = t61;
                        }
lc36:;
                    }
lb36:;
                }
                {
                    uint64_t t62 = gm_l3;
                    uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                    if (o64) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 192, 13);
                    uint64_t t65 = r63;
                    gm_l3 = t65;
                }
                {
                    gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                    gm_b16_8 t67 = t66;
                    gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                    void * t69 = gm_l1;
                    gm_b0_1 t70 = gm_f_code_block_6uq0k78((void *)NULL, cx, NULL, t68, t69);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                }
                goto ix27;
ie27:;
                gm_b16_8 t72 = gm_l5;
                gm_t8sds1gdern44 t73;
                gm_f_heading_rm86g4d((void *)&t73, t72);
                gm_t8sds1gdern44 t74 = t73;
                if (!((((uint8_t *)&t74))[24] != 0)) goto ie71;
                gm_tf1bjg81u0glv t75 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)&t74));
                gm_l7 = t75;
                {
                    uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
                    uint64_t t78 = GM_LD(uint64_t, gm_cus4oh838pa);
                    uint64_t r79; bool o80 = __builtin_add_overflow(t77, t78, &r79);
                    if (o80) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 195, 23);
                    uint64_t t81 = r79;
                    uint64_t t82 = (t81 < ((uint64_t)6ULL) ? t81 : ((uint64_t)6ULL));
                    gm_l8 = t82;
                }
                {
                    void * t83 = gm_l1;
                    uint64_t t84 = gm_l8;
                    gm_t4ce0jd3tdro4 t85;
                    GM_ST(uint64_t, ((uint8_t *)&t85 + 0), t84);
                    gm_tftb1odc739t5 t86 = gm_f_write_j9u0bpk(t83, t85);
                    gm_tftb1odc739t5 t87 = t86;
                    if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_src8pvk8vd1qnr1, 196, 13);
                    gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
                }
                {
                    gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                    void * t90 = gm_l1;
                    gm_b0_1 t91 = gm_f_inline_into_arphbtu((void *)NULL, cx, NULL, t89, t90);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                }
                {
                    void * t92 = gm_l1;
                    uint64_t t93 = gm_l8;
                    gm_t4ce0jd3tdro4 t94;
                    GM_ST(uint64_t, ((uint8_t *)&t94 + 0), t93);
                    gm_tftb1odc739t5 t95 = gm_f_write_3bvpoha(t92, t94);
                    gm_tftb1odc739t5 t96 = t95;
                    if (((((uint8_t *)&t96))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t96) + 1, gm_src8pvk8vd1qnr1, 198, 13);
                    gm_b0_1 t97 = GM_LD(gm_b0_1, (((uint8_t *)&t96) + 1));
                }
                {
                    uint64_t t98 = gm_l3;
                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                    if (o100) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 199, 13);
                    uint64_t t101 = r99;
                    gm_l3 = t101;
                }
                goto ix71;
ie71:;
                gm_b16_8 t103 = gm_l4;
                uint8_t t104 = gm_f_is_indented_d6n8n8l(t103);
                if (!t104) goto ie102;
                {
                    gm_f_list_epvducg((void *)&F.l9, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                }
                {
                    for (;;) {
                        uint64_t t107 = gm_l3;
                        uint64_t t108 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        uint8_t t109 = (t107 < t108);
                        if (!t109) goto lb106;
                        uint8_t t110 = 0;
                        uint64_t t111 = gm_l3;
                        uint64_t t112 = (uintptr_t)t111;
                        uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t112 >= (uintptr_t)(t113)) gm_trap_index(t112, t113, gm_src8pvk8vd1qnr1, 202, 50);
                        gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t112 * 16));
                        uint8_t t115 = gm_f_is_indented_d6n8n8l(t114);
                        if (!t115) {
                            uint64_t t116 = gm_l3;
                            uint64_t t117 = (uintptr_t)t116;
                            uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t117 >= (uintptr_t)(t118)) gm_trap_index(t117, t118, gm_src8pvk8vd1qnr1, 202, 68);
                            gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t117 * 16));
                            gm_b16_8 t120 = gm_f_trim_ebi5drb(t119);
                            gm_b16_8 t121 = t120;
                            uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&t121) + 8);
                            uint8_t t123 = ((t122 == ((uint64_t)0ULL)));
                            t110 = t123;
                        } else t110 = 1;
                        if (!t110) goto lb106;
                        {
                            void * t126 = (void *)((uint8_t *)&F.l9);
                            gm_b16_8 t127;
                            uint64_t t129 = gm_l3;
                            uint64_t t130 = (uintptr_t)t129;
                            uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t130 >= (uintptr_t)(t131)) gm_trap_index(t130, t131, gm_src8pvk8vd1qnr1, 203, 30);
                            uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t130 * 16) + 8);
                            uint8_t t133 = (t132 >= ((uint64_t)4ULL));
                            if (!t133) goto ie128;
                            uint64_t t135 = gm_l3;
                            uint64_t t136 = (uintptr_t)t135;
                            uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t136 >= (uintptr_t)(t137)) gm_trap_index(t136, t137, gm_src8pvk8vd1qnr1, 203, 50);
                            uint64_t t138 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t136 * 16) + 8);
                            void * t139 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t136 * 16)));
                            uint64_t t140 = (uintptr_t)((uint64_t)4ULL);
                            if (t140 > (uintptr_t)t138 || (uintptr_t)t138 > (uintptr_t)t138) gm_trap_slice(t140, (uintptr_t)t138, t138, gm_src8pvk8vd1qnr1, 203, 50);
                            gm_b16_8 t141;
                            GM_ST(void *, (uint8_t *)&t141, (void *)(((uint8_t *)t139 + t140 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)((uintptr_t)t138 - t140));
                            gm_b16_8 t142 = t141;
                            t127 = t142;
                            goto ix128;
ie128:;
                            gm_b16_8 t144;
                            GM_ST(void *, (uint8_t *)&t144, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(0));
                            gm_b16_8 t145 = t144;
                            t127 = t145;
ix128:;
                            gm_tftb1odc739t5 t146 = gm_f_push_3ef746b(t126, t127);
                            gm_tftb1odc739t5 t147 = t146;
                            if (((((uint8_t *)&t147))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t147) + 1, gm_src8pvk8vd1qnr1, 203, 17);
                            gm_b0_1 t148 = GM_LD(gm_b0_1, (((uint8_t *)&t147) + 1));
                        }
                        {
                            uint64_t t149 = gm_l3;
                            uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                            if (o151) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 204, 17);
                            uint64_t t152 = r150;
                            gm_l3 = t152;
                        }
lc106:;
                    }
lb106:;
                }
                {
                    for (;;) {
                        uint64_t t154 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                        uint8_t t155 = (t154 > ((uint64_t)0ULL));
                        if (!t155) goto lb153;
                        uint64_t t156 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                        uint64_t r157; bool o158 = __builtin_sub_overflow(t156, ((uint64_t)1ULL), &r157);
                        if (o158) gm_trap("integer overflow in `-`", gm_src8pvk8vd1qnr1, 206, 53);
                        uint64_t t159 = r157;
                        uint64_t t160 = (uintptr_t)t159;
                        uint64_t t161 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                        if (t160 >= (uintptr_t)(t161)) gm_trap_index(t160, t161, gm_src8pvk8vd1qnr1, 206, 42);
                        uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t160 * 16) + 8);
                        uint8_t t163 = ((t162 == ((uint64_t)0ULL)));
                        if (!t163) goto lb153;
                        {
                            {
                                void * t167 = (void *)((uint8_t *)&F.l9);
                                gm_th2lqbo77pvsc t168;
                                gm_f_pop_vr08sii((void *)&t168, t167);
                            }
                        }
lc153:;
                    }
lb153:;
                }
                {
                    gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                    gm_b16_8 t170 = t169;
                    gm_b16_8 t171 = GM_LD(gm_b16_8, ((uint8_t *)&t170));
                    void * t172 = gm_l1;
                    gm_b0_1 t173 = gm_f_code_block_6uq0k78((void *)NULL, cx, NULL, t171, t172);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                }
                goto ix102;
ie102:;
                gm_b16_8 t175 = gm_l5;
                gm_b16_8 t176;
                GM_ST(void *, (uint8_t *)&t176, (void *)(gm_sm5be4659ulci));
                GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(1));
                uint8_t t177 = gm_f_starts_with_6vvt01e(t175, t176);
                if (!t177) goto ie174;
                {
                    gm_f_list_epvducg((void *)&F.l10, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                }
                {
                    void * t179 = (void *)((uint8_t *)&F.l10);
                    uint64_t t180 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    void * t181 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)));
                    uint64_t t182 = (uintptr_t)((uint64_t)1ULL);
                    if (t182 > (uintptr_t)t180 || (uintptr_t)t180 > (uintptr_t)t180) gm_trap_slice(t182, (uintptr_t)t180, t180, gm_src8pvk8vd1qnr1, 210, 40);
                    gm_b16_8 t183;
                    GM_ST(void *, (uint8_t *)&t183, (void *)(((uint8_t *)t181 + t182 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)((uintptr_t)t180 - t182));
                    gm_b16_8 t184 = gm_f_strip_one_space_smv2rv8(t183);
                    gm_tftb1odc739t5 t185 = gm_f_push_3ef746b(t179, t184);
                    gm_tftb1odc739t5 t186 = t185;
                    if (((((uint8_t *)&t186))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t186) + 1, gm_src8pvk8vd1qnr1, 210, 13);
                    gm_b0_1 t187 = GM_LD(gm_b0_1, (((uint8_t *)&t186) + 1));
                }
                {
                    uint64_t t188 = gm_l3;
                    uint64_t r189; bool o190 = __builtin_add_overflow(t188, ((uint64_t)1ULL), &r189);
                    if (o190) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 211, 13);
                    uint64_t t191 = r189;
                    gm_l3 = t191;
                }
                {
                    for (;;) {
                        uint64_t t193 = gm_l3;
                        uint64_t t194 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        uint8_t t195 = (t193 < t194);
                        if (!t195) goto lb192;
                        uint64_t t196 = gm_l3;
                        uint64_t t197 = (uintptr_t)t196;
                        uint64_t t198 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t197 >= (uintptr_t)(t198)) gm_trap_index(t197, t198, gm_src8pvk8vd1qnr1, 212, 48);
                        gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t197 * 16));
                        gm_b16_8 t200 = gm_f_trim_start_lj2dhj3(t199);
                        gm_b16_8 t201;
                        GM_ST(void *, (uint8_t *)&t201, (void *)(gm_sm5be4659ulci));
                        GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(1));
                        uint8_t t202 = gm_f_starts_with_6vvt01e(t200, t201);
                        if (!t202) goto lb192;
                        {
                            void * t205 = (void *)((uint8_t *)&F.l10);
                            uint64_t t206 = gm_l3;
                            uint64_t t207 = (uintptr_t)t206;
                            uint64_t t208 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t207 >= (uintptr_t)(t208)) gm_trap_index(t207, t208, gm_src8pvk8vd1qnr1, 213, 55);
                            gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t207 * 16));
                            gm_b16_8 t210 = gm_f_trim_start_lj2dhj3(t209);
                            gm_b16_8 t211 = t210;
                            uint64_t t212 = GM_LD(uintptr_t, ((uint8_t *)&t211) + 8);
                            void * t213 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t211)));
                            uint64_t t214 = (uintptr_t)((uint64_t)1ULL);
                            if (t214 > (uintptr_t)t212 || (uintptr_t)t212 > (uintptr_t)t212) gm_trap_slice(t214, (uintptr_t)t212, t212, gm_src8pvk8vd1qnr1, 213, 44);
                            gm_b16_8 t215;
                            GM_ST(void *, (uint8_t *)&t215, (void *)(((uint8_t *)t213 + t214 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)((uintptr_t)t212 - t214));
                            gm_b16_8 t216 = gm_f_strip_one_space_smv2rv8(t215);
                            gm_tftb1odc739t5 t217 = gm_f_push_3ef746b(t205, t216);
                            gm_tftb1odc739t5 t218 = t217;
                            if (((((uint8_t *)&t218))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t218) + 1, gm_src8pvk8vd1qnr1, 213, 17);
                            gm_b0_1 t219 = GM_LD(gm_b0_1, (((uint8_t *)&t218) + 1));
                        }
                        {
                            uint64_t t220 = gm_l3;
                            uint64_t r221; bool o222 = __builtin_add_overflow(t220, ((uint64_t)1ULL), &r221);
                            if (o222) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 214, 17);
                            uint64_t t223 = r221;
                            gm_l3 = t223;
                        }
lc192:;
                    }
lb192:;
                }
                {
                    void * t224 = gm_l1;
                    gm_b16_8 t225;
                    GM_ST(void *, (uint8_t *)&t225, (void *)(gm_sm9cv8e73t8v3));
                    GM_ST(uintptr_t, (uint8_t *)&t225 + 8, (uintptr_t)(13));
                    gm_tftb1odc739t5 t226 = gm_f_extend_0cs85gs(t224, t225);
                    gm_tftb1odc739t5 t227 = t226;
                    if (((((uint8_t *)&t227))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t227) + 1, gm_src8pvk8vd1qnr1, 216, 13);
                    gm_b0_1 t228 = GM_LD(gm_b0_1, (((uint8_t *)&t227) + 1));
                }
                {
                    gm_b16_8 t229 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                    gm_b16_8 t230 = t229;
                    gm_b16_8 t231 = GM_LD(gm_b16_8, ((uint8_t *)&t230));
                    gm_b16_8 t232;
                    GM_ST(void *, (uint8_t *)&t232, (void *)(gm_so514oj5nbhcu));
                    GM_ST(uintptr_t, (uint8_t *)&t232 + 8, (uintptr_t)(1));
                    gm_b16_8 t233 = gm_f_join_e7fm1oo((void *)NULL, cx, NULL, t231, t232);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                    void * t234 = gm_l1;
                    gm_b0_1 t235 = gm_f_render_into_o6canda((void *)NULL, cx, NULL, t233, t234);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                }
                {
                    void * t236 = gm_l1;
                    gm_b16_8 t237;
                    GM_ST(void *, (uint8_t *)&t237, (void *)(gm_stq0db711o573));
                    GM_ST(uintptr_t, (uint8_t *)&t237 + 8, (uintptr_t)(14));
                    gm_tftb1odc739t5 t238 = gm_f_extend_0cs85gs(t236, t237);
                    gm_tftb1odc739t5 t239 = t238;
                    if (((((uint8_t *)&t239))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t239) + 1, gm_src8pvk8vd1qnr1, 218, 13);
                    gm_b0_1 t240 = GM_LD(gm_b0_1, (((uint8_t *)&t239) + 1));
                }
                goto ix174;
ie174:;
                gm_b16_8 t242 = gm_l5;
                gm_th3eueiipivq6 t243;
                gm_f_item_marker_kvo4318((void *)&t243, t242);
                gm_th3eueiipivq6 t244 = t243;
                uint8_t t245;
                if (((((uint8_t *)&t244))[16] != 0)) goto mn247;
                t245 = ((uint8_t)0);
                goto me246;
mn247:;
                t245 = ((uint8_t)1);
                goto me246;
mn248:;
                gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 219, 19);
me246:;
                if (!t245) goto ie241;
                {
                    gm_b16_8 t250 = gm_l2;
                    gm_b16_8 t251 = t250;
                    gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&t251));
                    uint64_t t253 = gm_l3;
                    void * t254 = gm_l1;
                    uint64_t t255 = gm_f_render_list_cq70hce((void *)NULL, cx, NULL, t252, t253, t254);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                    gm_l3 = t255;
                }
                goto ix241;
ie241:;
                gm_b16_8 t257 = gm_l5;
                gm_b16_8 t258;
                GM_ST(void *, (uint8_t *)&t258, (void *)(gm_s2u5posfkbgnq));
                GM_ST(uintptr_t, (uint8_t *)&t258 + 8, (uintptr_t)(1));
                uint8_t t259 = gm_f_starts_with_6vvt01e(t257, t258);
                if (!t259) goto ie256;
                uint64_t t260 = gm_l3;
                uint64_t r261; bool o262 = __builtin_add_overflow(t260, ((uint64_t)1ULL), &r261);
                if (o262) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 221, 42);
                uint64_t t263 = r261;
                uint64_t t264 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t265 = (t263 < t264);
                if (!t265) goto ie256;
                uint64_t t266 = gm_l3;
                uint64_t r267; bool o268 = __builtin_add_overflow(t266, ((uint64_t)1ULL), &r267);
                if (o268) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 221, 84);
                uint64_t t269 = r267;
                uint64_t t270 = (uintptr_t)t269;
                uint64_t t271 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t270 >= (uintptr_t)(t271)) gm_trap_index(t270, t271, gm_src8pvk8vd1qnr1, 221, 78);
                gm_b16_8 t272 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t270 * 16));
                uint8_t t273 = gm_f_is_table_rule_9r0ibck(t272);
                if (!t273) goto ie256;
                {
                    gm_b16_8 t275 = gm_l2;
                    gm_b16_8 t276 = t275;
                    gm_b16_8 t277 = GM_LD(gm_b16_8, ((uint8_t *)&t276));
                    uint64_t t278 = gm_l3;
                    void * t279 = gm_l1;
                    uint64_t t280 = gm_f_render_table_67oiva0((void *)NULL, cx, NULL, t277, t278, t279);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                    gm_l3 = t280;
                }
                goto ix256;
ie256:;
                {
                    gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                }
                {
                    void * t282 = (void *)((uint8_t *)&F.l11);
                    gm_b16_8 t283 = gm_l5;
                    gm_tftb1odc739t5 t284 = gm_f_push_3ef746b(t282, t283);
                    gm_tftb1odc739t5 t285 = t284;
                    if (((((uint8_t *)&t285))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t285) + 1, gm_src8pvk8vd1qnr1, 225, 13);
                    gm_b0_1 t286 = GM_LD(gm_b0_1, (((uint8_t *)&t285) + 1));
                }
                {
                    uint64_t t287 = gm_l3;
                    uint64_t r288; bool o289 = __builtin_add_overflow(t287, ((uint64_t)1ULL), &r288);
                    if (o289) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 226, 13);
                    uint64_t t290 = r288;
                    gm_l3 = t290;
                }
                {
                    for (;;) {
                        uint64_t t292 = gm_l3;
                        uint64_t t293 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        uint8_t t294 = (t292 < t293);
                        if (!t294) goto lb291;
                        uint64_t t295 = gm_l3;
                        uint64_t t296 = (uintptr_t)t295;
                        uint64_t t297 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t296 >= (uintptr_t)(t297)) gm_trap_index(t296, t297, gm_src8pvk8vd1qnr1, 227, 54);
                        gm_b16_8 t298 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t296 * 16));
                        uint8_t t299 = gm_f_starts_block_1sq7tuj(t298);
                        uint8_t t300 = (uint8_t)!t299;
                        if (!t300) goto lb291;
                        {
                            void * t303 = (void *)((uint8_t *)&F.l11);
                            uint64_t t304 = gm_l3;
                            uint64_t t305 = (uintptr_t)t304;
                            uint64_t t306 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t305 >= (uintptr_t)(t306)) gm_trap_index(t305, t306, gm_src8pvk8vd1qnr1, 228, 32);
                            gm_b16_8 t307 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t305 * 16));
                            gm_b16_8 t308 = gm_f_trim_ebi5drb(t307);
                            gm_tftb1odc739t5 t309 = gm_f_push_3ef746b(t303, t308);
                            gm_tftb1odc739t5 t310 = t309;
                            if (((((uint8_t *)&t310))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t310) + 1, gm_src8pvk8vd1qnr1, 228, 17);
                            gm_b0_1 t311 = GM_LD(gm_b0_1, (((uint8_t *)&t310) + 1));
                        }
                        {
                            uint64_t t312 = gm_l3;
                            uint64_t r313; bool o314 = __builtin_add_overflow(t312, ((uint64_t)1ULL), &r313);
                            if (o314) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 229, 17);
                            uint64_t t315 = r313;
                            gm_l3 = t315;
                        }
lc291:;
                    }
lb291:;
                }
                {
                    void * t316 = gm_l1;
                    gm_b16_8 t317;
                    GM_ST(void *, (uint8_t *)&t317, (void *)(gm_sciqh24mne54a));
                    GM_ST(uintptr_t, (uint8_t *)&t317 + 8, (uintptr_t)(3));
                    gm_tftb1odc739t5 t318 = gm_f_extend_0cs85gs(t316, t317);
                    gm_tftb1odc739t5 t319 = t318;
                    if (((((uint8_t *)&t319))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t319) + 1, gm_src8pvk8vd1qnr1, 231, 13);
                    gm_b0_1 t320 = GM_LD(gm_b0_1, (((uint8_t *)&t319) + 1));
                }
                {
                    gm_b16_8 t321 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                    gm_b16_8 t322 = t321;
                    gm_b16_8 t323 = GM_LD(gm_b16_8, ((uint8_t *)&t322));
                    gm_b16_8 t324;
                    GM_ST(void *, (uint8_t *)&t324, (void *)(gm_s3p0rbs9hc3lg));
                    GM_ST(uintptr_t, (uint8_t *)&t324 + 8, (uintptr_t)(1));
                    gm_b16_8 t325 = gm_f_join_e7fm1oo((void *)NULL, cx, NULL, t323, t324);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                    void * t326 = gm_l1;
                    gm_b0_1 t327 = gm_f_inline_into_arphbtu((void *)NULL, cx, NULL, t325, t326);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                }
                {
                    void * t328 = gm_l1;
                    gm_b16_8 t329;
                    GM_ST(void *, (uint8_t *)&t329, (void *)(gm_s6pceda0js0g8));
                    GM_ST(uintptr_t, (uint8_t *)&t329 + 8, (uintptr_t)(5));
                    gm_tftb1odc739t5 t330 = gm_f_extend_0cs85gs(t328, t329);
                    gm_tftb1odc739t5 t331 = t330;
                    if (((((uint8_t *)&t331))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t331) + 1, gm_src8pvk8vd1qnr1, 233, 13);
                    gm_b0_1 t332 = GM_LD(gm_b0_1, (((uint8_t *)&t331) + 1));
                }
ix256:;
ix241:;
ix174:;
ix102:;
ix71:;
ix27:;
ix16:;
            }
lc4:;
        }
gm_ll8:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb4; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc4; }
        }
        goto gm_lad;
lb4:;
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
__attribute__((aligned(4))) uint64_t gm_f_render_list_cq70hce(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, void * a2) {
    struct gm_fr_render_list_cq70hce F;
    gm_b16_8 gm_l0; /* lines */
    uint64_t gm_l1; /* start */
    void * gm_l2; /* out */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* numbered */
    gm_b16_8 gm_l5; /* tag */
    gm_b16_8 gm_l6; /* t */
    gm_tf5srh4mp4b3i gm_l7; /* marker */
    gm_b16_8 gm_l9; /* l */
    gm_b16_8 gm_l10; /* text */
    uint8_t gm_l11; /* nested */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* j */
    gm_b16_8 gm_l15; /* b */
    GM_STACK_CHECK(&F, 1685u);
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
        uint64_t t3 = gm_l3;
        uint64_t t4 = (uintptr_t)t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_src8pvk8vd1qnr1, 282, 41);
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t4 * 16));
        gm_b16_8 t7 = gm_f_trim_start_lj2dhj3(t6);
        gm_th3eueiipivq6 t8;
        gm_f_item_marker_kvo4318((void *)&t8, t7);
        gm_th3eueiipivq6 t9 = t8;
        gm_tf5srh4mp4b3i t10;
        if (((((uint8_t *)&t9))[16] != 0)) {
            gm_tf5srh4mp4b3i t11 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            uint64_t t12 = gm_l3;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_tf5srh4mp4b3i t14 = t10;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t14) + 8));
        gm_l4 = t15;
    }
    {
        gm_b16_8 t16;
        uint8_t t18 = gm_l4;
        if (!t18) goto ie17;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s0u0sleiibm5v));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
        gm_b16_8 t21 = t20;
        t16 = t21;
        goto ix17;
ie17:;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sdogehgrilpeb));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(2));
        gm_b16_8 t24 = t23;
        t16 = t24;
ix17:;
        gm_l5 = t16;
    }
    {
        void * t25 = gm_l2;
        gm_b16_8 t26 = gm_l5;
        gm_tnpifv20irnph t27;
        GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t26);
        gm_tftb1odc739t5 t28 = gm_f_write_fgbjr48(t25, t27);
        gm_tftb1odc739t5 t29 = t28;
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_src8pvk8vd1qnr1, 284, 5);
        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
    }
    {
        for (;;) {
            uint64_t t32 = gm_l3;
            uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t34 = (t32 < t33);
            if (!t34) goto lb31;
            {
                uint64_t t37 = gm_l3;
                uint64_t t38 = (uintptr_t)t37;
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_src8pvk8vd1qnr1, 286, 25);
                gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t38 * 16));
                gm_b16_8 t41 = gm_f_trim_start_lj2dhj3(t40);
                gm_l6 = t41;
            }
            {
                gm_b16_8 t42 = gm_l6;
                gm_th3eueiipivq6 t43;
                gm_f_item_marker_kvo4318((void *)&t43, t42);
                gm_th3eueiipivq6 t44 = t43;
                gm_tf5srh4mp4b3i t45;
                if (((((uint8_t *)&t44))[16] != 0)) {
                    gm_tf5srh4mp4b3i t46 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&t44) + 0));
                    t45 = t46;
                } else {
                    goto lb31;
                }
                gm_l7 = t45;
            }
            {
                uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t49 = gm_l4;
                uint8_t t50 = (!(t48 == t49));
                if (!t50) goto ie47;
                {
                    goto lb31;
                }
                goto ix47;
ie47:;
ix47:;
            }
            {
                gm_f_list_epvducg((void *)&F.l8, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll35;
                }
            }
            {
                void * t52 = (void *)((uint8_t *)&F.l8);
                uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                void * t54 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
                uint64_t t56 = (uintptr_t)t55;
                if (t56 > (uintptr_t)t53 || (uintptr_t)t53 > (uintptr_t)t53) gm_trap_slice(t56, (uintptr_t)t53, t53, gm_src8pvk8vd1qnr1, 290, 19);
                gm_b16_8 t57;
                GM_ST(void *, (uint8_t *)&t57, (void *)(((uint8_t *)t54 + t56 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)((uintptr_t)t53 - t56));
                gm_tftb1odc739t5 t58 = gm_f_push_3ef746b(t52, t57);
                gm_tftb1odc739t5 t59 = t58;
                if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_src8pvk8vd1qnr1, 290, 9);
                gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
            }
            {
                uint64_t t61 = gm_l3;
                uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                if (o63) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 291, 9);
                uint64_t t64 = r62;
                gm_l3 = t64;
            }
            {
                for (;;) {
                    uint64_t t66 = gm_l3;
                    uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t68 = (t66 < t67);
                    if (!t68) goto lb65;
                    {
                        uint64_t t71 = gm_l3;
                        uint64_t t72 = (uintptr_t)t71;
                        uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t72 >= (uintptr_t)(t73)) gm_trap_index(t72, t73, gm_src8pvk8vd1qnr1, 295, 18);
                        gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t72 * 16));
                        gm_l9 = t74;
                    }
                    {
                        gm_b16_8 t76 = gm_l9;
                        gm_b16_8 t77 = gm_f_trim_ebi5drb(t76);
                        gm_b16_8 t78 = t77;
                        uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&t78) + 8);
                        uint8_t t80 = ((t79 == ((uint64_t)0ULL)));
                        if (!t80) goto ie75;
                        {
                            uint64_t t83 = gm_l3;
                            uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                            if (o85) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 297, 20);
                            uint64_t t86 = r84;
                            uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            uint8_t t88 = (t86 < t87);
                            if (!t88) goto ie82;
                            uint64_t t89 = gm_l3;
                            uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                            if (o91) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 297, 48);
                            uint64_t t92 = r90;
                            uint64_t t93 = (uintptr_t)t92;
                            uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t93 >= (uintptr_t)(t94)) gm_trap_index(t93, t94, gm_src8pvk8vd1qnr1, 297, 42);
                            gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t93 * 16));
                            gm_b16_8 t96;
                            GM_ST(void *, (uint8_t *)&t96, (void *)(gm_sgankvlme129s));
                            GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(2));
                            uint8_t t97 = gm_f_starts_with_6vvt01e(t95, t96);
                            if (!t97) goto ie82;
                            {
                                void * t99 = (void *)((uint8_t *)&F.l8);
                                gm_b16_8 t100;
                                GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(0));
                                gm_tftb1odc739t5 t101 = gm_f_push_3ef746b(t99, t100);
                                gm_tftb1odc739t5 t102 = t101;
                                if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 1, gm_src8pvk8vd1qnr1, 298, 21);
                                gm_b0_1 t103 = GM_LD(gm_b0_1, (((uint8_t *)&t102) + 1));
                            }
                            {
                                uint64_t t104 = gm_l3;
                                uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                                if (o106) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 299, 21);
                                uint64_t t107 = r105;
                                gm_l3 = t107;
                            }
                            {
                                goto lc65;
                            }
                            goto ix82;
ie82:;
ix82:;
                        }
                        {
                            goto lb65;
                        }
                        goto ix75;
ie75:;
ix75:;
                    }
                    {
                        uint8_t t109 = 0;
                        gm_b16_8 t110 = gm_l9;
                        gm_b16_8 t111;
                        GM_ST(void *, (uint8_t *)&t111, (void *)(gm_sgankvlme129s));
                        GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(2));
                        uint8_t t112 = gm_f_starts_with_6vvt01e(t110, t111);
                        if (!t112) {
                            gm_b16_8 t113 = gm_l9;
                            uint8_t t114 = gm_f_starts_block_1sq7tuj(t113);
                            uint8_t t115 = (uint8_t)!t114;
                            t109 = t115;
                        } else t109 = 1;
                        if (!t109) goto ie108;
                        {
                            void * t117 = (void *)((uint8_t *)&F.l8);
                            gm_b16_8 t118 = gm_l9;
                            gm_b16_8 t119 = gm_f_trim_start_lj2dhj3(t118);
                            gm_tftb1odc739t5 t120 = gm_f_push_3ef746b(t117, t119);
                            gm_tftb1odc739t5 t121 = t120;
                            if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 1, gm_src8pvk8vd1qnr1, 305, 17);
                            gm_b0_1 t122 = GM_LD(gm_b0_1, (((uint8_t *)&t121) + 1));
                        }
                        {
                            uint64_t t123 = gm_l3;
                            uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                            if (o125) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 306, 17);
                            uint64_t t126 = r124;
                            gm_l3 = t126;
                        }
                        goto ix108;
ie108:;
                        {
                            goto lb65;
                        }
ix108:;
                    }
lc65:;
                }
lb65:;
            }
            {
                gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                gm_b16_8 t129 = t128;
                gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                gm_b16_8 t131;
                GM_ST(void *, (uint8_t *)&t131, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(1));
                gm_b16_8 t132 = gm_f_join_e7fm1oo((void *)NULL, cx, NULL, t130, t131);
                if (gm_unw.kind) {
                    goto gm_ll35;
                }
                gm_l10 = t132;
            }
            {
                gm_l11 = ((uint8_t)0);
            }
            {
                {
                    gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                    gm_b16_8 t135 = t134;
                    gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&t135));
                    gm_l12 = t136;
                }
                {
                    gm_l13 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t138 = gm_l13;
                        uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        uint8_t t140 = (t138 < t139);
                        if (!t140) goto lb137;
                        {
                            gm_tf1bjg81u0glv t143;
                            uint64_t t144 = gm_l13;
                            GM_ST(uint64_t, (uint8_t *)&t143 + 0, t144);
                            uint64_t t145 = gm_l13;
                            uint64_t t146 = (uintptr_t)t145;
                            uint64_t t147 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t146 * 16));
                            GM_ST(gm_b16_8, (uint8_t *)&t143 + 8, t148);
                            gm_tf1bjg81u0glv t149 = t143;
                            uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&t149) + 0));
                            gm_l14 = t152;
                            gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&t149) + 8));
                            gm_l15 = t153;
                            {
                                uint8_t t156 = 0;
                                uint64_t t157 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                uint8_t t158 = ((t157 == ((uint64_t)0ULL)));
                                if (!t158) {
                                    uint8_t t159 = 0;
                                    uint64_t t160 = gm_l14;
                                    uint8_t t161 = (t160 > ((uint64_t)0ULL));
                                    if (t161) {
                                        gm_b16_8 t162 = gm_l15;
                                        gm_th3eueiipivq6 t163;
                                        gm_f_item_marker_kvo4318((void *)&t163, t162);
                                        gm_th3eueiipivq6 t164 = t163;
                                        uint8_t t165;
                                        if (((((uint8_t *)&t164))[16] != 0)) goto mn167;
                                        t165 = ((uint8_t)0);
                                        goto me166;
mn167:;
                                        t165 = ((uint8_t)1);
                                        goto me166;
mn168:;
                                        gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 314, 41);
me166:;
                                        t159 = t165;
                                    } else t159 = 0;
                                    t156 = t159;
                                } else t156 = 1;
                                if (!t156) goto ie155;
                                {
                                    gm_l11 = ((uint8_t)1);
                                }
                                goto ix155;
ie155:;
ix155:;
                            }
                            goto me150;
mn151:;
                            gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 313, 9);
me150:;
                        }
lc137:;
                        {
                            uint64_t t171 = gm_l13;
                            uint64_t r172; bool o173 = __builtin_add_overflow(t171, ((uint64_t)1ULL), &r172);
                            uint64_t t174 = r172;
                            gm_l13 = t174;
                        }
                    }
lb137:;
                }
            }
            {
                uint8_t t176 = gm_l11;
                if (!t176) goto ie175;
                {
                    void * t178 = gm_l2;
                    gm_b16_8 t179;
                    GM_ST(void *, (uint8_t *)&t179, (void *)(gm_siselrhsqskvi));
                    GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(5));
                    gm_tftb1odc739t5 t180 = gm_f_extend_0cs85gs(t178, t179);
                    gm_tftb1odc739t5 t181 = t180;
                    if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 1, gm_src8pvk8vd1qnr1, 317, 13);
                    gm_b0_1 t182 = GM_LD(gm_b0_1, (((uint8_t *)&t181) + 1));
                }
                {
                    gm_b16_8 t183 = gm_l10;
                    void * t184 = gm_l2;
                    gm_b0_1 t185 = gm_f_render_into_o6canda((void *)NULL, cx, NULL, t183, t184);
                    if (gm_unw.kind) {
                        goto gm_ll35;
                    }
                }
                {
                    void * t186 = gm_l2;
                    gm_b16_8 t187;
                    GM_ST(void *, (uint8_t *)&t187, (void *)(gm_snmep73bl34j8));
                    GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(6));
                    gm_tftb1odc739t5 t188 = gm_f_extend_0cs85gs(t186, t187);
                    gm_tftb1odc739t5 t189 = t188;
                    if (((((uint8_t *)&t189))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t189) + 1, gm_src8pvk8vd1qnr1, 319, 13);
                    gm_b0_1 t190 = GM_LD(gm_b0_1, (((uint8_t *)&t189) + 1));
                }
                goto ix175;
ie175:;
                {
                    void * t192 = gm_l2;
                    gm_b16_8 t193;
                    GM_ST(void *, (uint8_t *)&t193, (void *)(gm_slopv2r8r4od2));
                    GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(4));
                    gm_tftb1odc739t5 t194 = gm_f_extend_0cs85gs(t192, t193);
                    gm_tftb1odc739t5 t195 = t194;
                    if (((((uint8_t *)&t195))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t195) + 1, gm_src8pvk8vd1qnr1, 321, 13);
                    gm_b0_1 t196 = GM_LD(gm_b0_1, (((uint8_t *)&t195) + 1));
                }
                {
                    gm_b16_8 t197 = gm_l10;
                    gm_b16_8 t198 = gm_f_unwrap_lines_59i2isg((void *)NULL, cx, NULL, t197);
                    if (gm_unw.kind) {
                        goto gm_ll35;
                    }
                    void * t199 = gm_l2;
                    gm_b0_1 t200 = gm_f_inline_into_arphbtu((void *)NULL, cx, NULL, t198, t199);
                    if (gm_unw.kind) {
                        goto gm_ll35;
                    }
                }
                {
                    void * t201 = gm_l2;
                    gm_b16_8 t202;
                    GM_ST(void *, (uint8_t *)&t202, (void *)(gm_snmep73bl34j8));
                    GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(6));
                    gm_tftb1odc739t5 t203 = gm_f_extend_0cs85gs(t201, t202);
                    gm_tftb1odc739t5 t204 = t203;
                    if (((((uint8_t *)&t204))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t204) + 1, gm_src8pvk8vd1qnr1, 323, 13);
                    gm_b0_1 t205 = GM_LD(gm_b0_1, (((uint8_t *)&t204) + 1));
                }
ix175:;
            }
lc31:;
        }
gm_ll35:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb31; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc31; }
        }
        goto gm_lad;
lb31:;
    }
    {
        void * t206 = gm_l2;
        gm_b16_8 t207 = gm_l5;
        gm_tnpifv20irnph t208;
        GM_ST(gm_b16_8, ((uint8_t *)&t208 + 0), t207);
        gm_tftb1odc739t5 t209 = gm_f_write_6a3aen6(t206, t208);
        gm_tftb1odc739t5 t210 = t209;
        if (((((uint8_t *)&t210))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t210) + 1, gm_src8pvk8vd1qnr1, 326, 5);
        gm_b0_1 t211 = GM_LD(gm_b0_1, (((uint8_t *)&t210) + 1));
    }
    {
        uint64_t t212 = gm_l3;
        uint8_t t213 = 0;
        F.ret = t212;
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
__attribute__((aligned(4))) uint64_t gm_f_render_table_67oiva0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, void * a2) {
    struct gm_fr_render_table_67oiva0 F;
    gm_b16_8 gm_l0; /* lines */
    uint64_t gm_l1; /* start */
    void * gm_l2; /* out */
    uint64_t gm_l3; /* i */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* c */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* c */
    GM_STACK_CHECK(&F, 833u);
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
        void * t3 = gm_l2;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sblomia8fb3mu));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(19));
        gm_tftb1odc739t5 t5 = gm_f_extend_0cs85gs(t3, t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_src8pvk8vd1qnr1, 361, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        {
            uint64_t t9 = gm_l3;
            uint64_t t10 = (uintptr_t)t9;
            uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_src8pvk8vd1qnr1, 362, 20);
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t10 * 16));
            gm_b16_8 t13 = gm_f_cells_ud23es9((void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t13;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l5;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l5;
                    uint64_t t21 = (uintptr_t)t20;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t21 * 16));
                    gm_l6 = t25;
                    {
                        void * t27 = gm_l2;
                        gm_b16_8 t28;
                        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_shnk1stovqggp));
                        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t29 = gm_f_extend_0cs85gs(t27, t28);
                        gm_tftb1odc739t5 t30 = t29;
                        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_src8pvk8vd1qnr1, 363, 9);
                        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                    }
                    {
                        gm_b16_8 t32 = gm_l6;
                        void * t33 = gm_l2;
                        gm_b0_1 t34 = gm_f_inline_into_arphbtu((void *)NULL, cx, NULL, t32, t33);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    {
                        void * t35 = gm_l2;
                        gm_b16_8 t36;
                        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_savlppb39i8g6));
                        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(5));
                        gm_tftb1odc739t5 t37 = gm_f_extend_0cs85gs(t35, t36);
                        gm_tftb1odc739t5 t38 = t37;
                        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_src8pvk8vd1qnr1, 365, 9);
                        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 362, 5);
me23:;
                }
lc14:;
                {
                    uint64_t t41 = gm_l5;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l5 = t44;
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
        void * t45 = gm_l2;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s0c4i5le3mc2l));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(22));
        gm_tftb1odc739t5 t47 = gm_f_extend_0cs85gs(t45, t46);
        gm_tftb1odc739t5 t48 = t47;
        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_src8pvk8vd1qnr1, 367, 5);
        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
    }
    {
        uint64_t t50 = gm_l3;
        uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)2ULL), &r51);
        if (o52) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 368, 5);
        uint64_t t53 = r51;
        gm_l3 = t53;
    }
    {
        for (;;) {
            uint64_t t55 = gm_l3;
            uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t57 = (t55 < t56);
            if (!t57) goto lb54;
            uint64_t t58 = gm_l3;
            uint64_t t59 = (uintptr_t)t58;
            uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_src8pvk8vd1qnr1, 369, 40);
            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t59 * 16));
            gm_b16_8 t62 = gm_f_trim_start_lj2dhj3(t61);
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(gm_s2u5posfkbgnq));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(1));
            uint8_t t64 = gm_f_starts_with_6vvt01e(t62, t63);
            if (!t64) goto lb54;
            {
                void * t67 = gm_l2;
                gm_b16_8 t68;
                GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s2r68j08i2lia));
                GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(4));
                gm_tftb1odc739t5 t69 = gm_f_extend_0cs85gs(t67, t68);
                gm_tftb1odc739t5 t70 = t69;
                if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_src8pvk8vd1qnr1, 370, 9);
                gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
            }
            {
                {
                    uint64_t t73 = gm_l3;
                    uint64_t t74 = (uintptr_t)t73;
                    uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t74 >= (uintptr_t)(t75)) gm_trap_index(t74, t75, gm_src8pvk8vd1qnr1, 371, 24);
                    gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t74 * 16));
                    gm_b16_8 t77 = gm_f_cells_ud23es9((void *)NULL, cx, NULL, t76);
                    if (gm_unw.kind) {
                        goto gm_ll65;
                    }
                    gm_l7 = t77;
                }
                {
                    gm_l8 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t79 = gm_l8;
                        uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t81 = (t79 < t80);
                        if (!t81) goto lb78;
                        {
                            uint64_t t84 = gm_l8;
                            uint64_t t85 = (uintptr_t)t84;
                            uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t85 * 16));
                            gm_l9 = t89;
                            {
                                void * t91 = gm_l2;
                                gm_b16_8 t92;
                                GM_ST(void *, (uint8_t *)&t92, (void *)(gm_shb921rk4rkpo));
                                GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(4));
                                gm_tftb1odc739t5 t93 = gm_f_extend_0cs85gs(t91, t92);
                                gm_tftb1odc739t5 t94 = t93;
                                if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_src8pvk8vd1qnr1, 372, 13);
                                gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
                            }
                            {
                                gm_b16_8 t96 = gm_l9;
                                void * t97 = gm_l2;
                                gm_b0_1 t98 = gm_f_inline_into_arphbtu((void *)NULL, cx, NULL, t96, t97);
                                if (gm_unw.kind) {
                                    goto gm_ll82;
                                }
                            }
                            {
                                void * t99 = gm_l2;
                                gm_b16_8 t100;
                                GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sg344a2p7u6i6));
                                GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(5));
                                gm_tftb1odc739t5 t101 = gm_f_extend_0cs85gs(t99, t100);
                                gm_tftb1odc739t5 t102 = t101;
                                if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 1, gm_src8pvk8vd1qnr1, 374, 13);
                                gm_b0_1 t103 = GM_LD(gm_b0_1, (((uint8_t *)&t102) + 1));
                            }
                            goto me87;
mn88:;
                            gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 371, 9);
me87:;
                        }
lc78:;
                        {
                            uint64_t t105 = gm_l8;
                            uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                            uint64_t t108 = r106;
                            gm_l8 = t108;
                        }
                    }
gm_ll82:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb78; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc78; }
                    }
                    goto gm_ll65;
lb78:;
                }
            }
            {
                void * t109 = gm_l2;
                gm_b16_8 t110;
                GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sbdlpmcdardns));
                GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(6));
                gm_tftb1odc739t5 t111 = gm_f_extend_0cs85gs(t109, t110);
                gm_tftb1odc739t5 t112 = t111;
                if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 1, gm_src8pvk8vd1qnr1, 376, 9);
                gm_b0_1 t113 = GM_LD(gm_b0_1, (((uint8_t *)&t112) + 1));
            }
            {
                uint64_t t114 = gm_l3;
                uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
                if (o116) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 377, 9);
                uint64_t t117 = r115;
                gm_l3 = t117;
            }
lc54:;
        }
gm_ll65:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb54; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc54; }
        }
        goto gm_lad;
lb54:;
    }
    {
        void * t118 = gm_l2;
        gm_b16_8 t119;
        GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sel2d9vtaa0ba));
        GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(18));
        gm_tftb1odc739t5 t120 = gm_f_extend_0cs85gs(t118, t119);
        gm_tftb1odc739t5 t121 = t120;
        if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 1, gm_src8pvk8vd1qnr1, 379, 5);
        gm_b0_1 t122 = GM_LD(gm_b0_1, (((uint8_t *)&t121) + 1));
    }
    {
        uint64_t t123 = gm_l3;
        uint8_t t124 = 0;
        F.ret = t123;
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
inline __attribute__((aligned(4))) uint8_t gm_f_safe_url_bhj1jha(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_safe_url_bhj1jha F;
    gm_b16_8 gm_l0; /* url */
    uint64_t gm_l1; /* colon */
    gm_b16_8 gm_l2; /* scheme */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 377u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_tf5srh4mp4b3i t3 = gm_f_index_of_hdeg351(t2, ((uint8_t)58ULL));
        gm_tf5srh4mp4b3i t4 = t3;
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t11 = gm_l1;
        uint64_t t12 = (uintptr_t)t11;
        if (t10 > t12 || t12 > (uintptr_t)t8) gm_trap_slice(t10, t12, t8, gm_src8pvk8vd1qnr1, 516, 15);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t9 + t10 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t10));
        gm_l2 = t13;
    }
    {
        {
            gm_b16_8 t15 = gm_l2;
            gm_l3 = t15;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t17 = gm_l4;
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t19 = (t17 < t18);
                if (!t19) goto lb16;
                {
                    uint64_t t22 = gm_l4;
                    uint64_t t23 = (uintptr_t)t22;
                    uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t23 * 1));
                    uint8_t t26 = t25;
                    uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)&t26));
                    gm_l5 = t29;
                    {
                        uint8_t t32 = gm_l5;
                        gm_toh0ootodhjue t33;
                        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)47ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t33 + 1), ((uint8_t)35ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t33 + 2), ((uint8_t)63ULL));
                        gm_toh0ootodhjue t34 = t33;
                        uint8_t t35 = 0;
                        uint8_t t36 = t32;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t34) + 0, ((uint8_t *)&t36))) t35 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t34) + 1, ((uint8_t *)&t36))) t35 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t34) + 2, ((uint8_t *)&t36))) t35 = 1;
                        uint8_t t37 = (uint8_t)(t35);
                        if (!t37) goto ie31;
                        {
                            uint8_t t39 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix31;
ie31:;
ix31:;
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 517, 5);
me27:;
                }
lc16:;
                {
                    uint64_t t41 = gm_l4;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l4 = t44;
                }
            }
lb16:;
        }
    }
    {
        uint8_t t45 = 0;
        uint8_t t46 = 0;
        gm_b16_8 t47 = gm_l2;
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s2t6ld4n1pdgu));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(4));
        uint8_t t49 = gm_f_eq_ignore_case_qqsqlkn(t47, t48);
        if (!t49) {
            gm_b16_8 t50 = gm_l2;
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s7i070auf0cel));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(5));
            uint8_t t52 = gm_f_eq_ignore_case_qqsqlkn(t50, t51);
            t46 = t52;
        } else t46 = 1;
        if (!t46) {
            gm_b16_8 t53 = gm_l2;
            gm_b16_8 t54;
            GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sm9lq3mvrfi15));
            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(6));
            uint8_t t55 = gm_f_eq_ignore_case_qqsqlkn(t53, t54);
            t45 = t55;
        } else t45 = 1;
        uint8_t t56 = 0;
        F.ret = t45;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_squash_2iiihih(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_squash_2iiihih F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* pending */
    uint64_t gm_l4; /* n */
    GM_STACK_CHECK(&F, 271u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l2;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                gm_b16_8 t8 = gm_l0;
                uint64_t t9 = gm_l2;
                uint64_t t10 = gm_f_ws_at_dchgqis(t8, t9);
                gm_l4 = t10;
            }
            {
                uint64_t t12 = gm_l4;
                uint8_t t13 = (t12 > ((uint64_t)0ULL));
                if (!t13) goto ie11;
                {
                    uint64_t t15 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
                    uint8_t t16 = (t15 > ((uint64_t)0ULL));
                    gm_l3 = t16;
                }
                {
                    uint64_t t17 = gm_l2;
                    uint64_t t18 = gm_l4;
                    uint64_t r19; bool o20 = __builtin_add_overflow(t17, t18, &r19);
                    if (o20) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 102, 13);
                    uint64_t t21 = r19;
                    gm_l2 = t21;
                }
                goto ix11;
ie11:;
                {
                    uint8_t t24 = gm_l3;
                    if (!t24) goto ie23;
                    {
                        void * t26 = (void *)((uint8_t *)&F.l1);
                        gm_tftb1odc739t5 t27 = gm_f_push_t83rg2m(t26, ((uint8_t)32ULL));
                        gm_tftb1odc739t5 t28 = t27;
                        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_src8pvk8vd1qnr1, 104, 26);
                        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
                    }
                    goto ix23;
ie23:;
ix23:;
                }
                {
                    gm_l3 = ((uint8_t)0);
                }
                {
                    void * t30 = (void *)((uint8_t *)&F.l1);
                    uint64_t t31 = gm_l2;
                    uint64_t t32 = (uintptr_t)t31;
                    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_src8pvk8vd1qnr1, 106, 22);
                    uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t32 * 1));
                    gm_tftb1odc739t5 t35 = gm_f_push_t83rg2m(t30, t34);
                    gm_tftb1odc739t5 t36 = t35;
                    if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_src8pvk8vd1qnr1, 106, 13);
                    gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                }
                {
                    uint64_t t38 = gm_l2;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 107, 13);
                    uint64_t t41 = r39;
                    gm_l2 = t41;
                }
ix11:;
            }
lc2:;
        }
lb2:;
    }
    {
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t43 = t42;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
        uint8_t t45 = 0;
        F.ret = t44;
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
inline __attribute__((aligned(4))) uint8_t gm_f_starts_block_1sq7tuj(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_block_1sq7tuj F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* t */
    GM_STACK_CHECK(&F, 374u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_trim_start_lj2dhj3(t2);
        gm_l1 = t3;
    }
    {
        uint8_t t4 = 0;
        uint8_t t5 = 0;
        uint8_t t6 = 0;
        uint8_t t7 = 0;
        uint8_t t8 = 0;
        gm_b16_8 t9 = gm_l0;
        gm_b16_8 t10 = gm_f_trim_ebi5drb(t9);
        gm_b16_8 t11 = t10;
        uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&t11) + 8);
        uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
        if (!t13) {
            gm_b16_8 t14 = gm_l1;
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s7un5bnoqg548));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(3));
            uint8_t t16 = gm_f_starts_with_6vvt01e(t14, t15);
            t8 = t16;
        } else t8 = 1;
        if (!t8) {
            gm_b16_8 t17 = gm_l1;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sm5be4659ulci));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            uint8_t t19 = gm_f_starts_with_6vvt01e(t17, t18);
            t7 = t19;
        } else t7 = 1;
        if (!t7) {
            gm_b16_8 t20 = gm_l1;
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s2u5posfkbgnq));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            uint8_t t22 = gm_f_starts_with_6vvt01e(t20, t21);
            t6 = t22;
        } else t6 = 1;
        if (!t6) {
            gm_b16_8 t23 = gm_l1;
            gm_t8sds1gdern44 t24;
            gm_f_heading_rm86g4d((void *)&t24, t23);
            gm_t8sds1gdern44 t25 = t24;
            uint8_t t26;
            if (((((uint8_t *)&t25))[24] != 0)) goto mn28;
            t26 = ((uint8_t)0);
            goto me27;
mn28:;
            t26 = ((uint8_t)1);
            goto me27;
mn29:;
            gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 277, 103);
me27:;
            t5 = t26;
        } else t5 = 1;
        if (!t5) {
            gm_b16_8 t30 = gm_l1;
            gm_th3eueiipivq6 t31;
            gm_f_item_marker_kvo4318((void *)&t31, t30);
            gm_th3eueiipivq6 t32 = t31;
            uint8_t t33;
            if (((((uint8_t *)&t32))[16] != 0)) goto mn35;
            t33 = ((uint8_t)0);
            goto me34;
mn35:;
            t33 = ((uint8_t)1);
            goto me34;
mn36:;
            gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 277, 129);
me34:;
            t4 = t33;
        } else t4 = 1;
        uint8_t t37 = 0;
        F.ret = t4;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_strip_one_space_smv2rv8(gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_trim_ebi5drb(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_2059dot(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_end_2059dot F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* end */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l1 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l1;
            uint8_t t5 = (t4 > ((uint64_t)0ULL));
            if (!t5) goto lb3;
            {
                gm_b16_8 t8 = gm_l0;
                uint64_t t9 = gm_l1;
                uint64_t t10 = gm_f_ws_before_jst10le(t8, t9);
                gm_l2 = t10;
            }
            {
                uint64_t t12 = gm_l2;
                uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
                if (!t13) goto ie11;
                {
                    goto lb3;
                }
                goto ix11;
ie11:;
ix11:;
            }
            {
                uint64_t t15 = gm_l1;
                uint64_t t16 = gm_l2;
                uint64_t r17; bool o18 = __builtin_sub_overflow(t15, t16, &r17);
                if (o18) gm_trap("integer overflow in `-`", gm_src8pvk8vd1qnr1, 84, 9);
                uint64_t t19 = r17;
                gm_l1 = t19;
            }
lc3:;
        }
lb3:;
    }
    {
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t21 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l1;
        uint64_t t24 = (uintptr_t)t23;
        if (t22 > t24 || t24 > (uintptr_t)t20) gm_trap_slice(t22, t24, t20, gm_src8pvk8vd1qnr1, 86, 12);
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t21 + t22 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24 - t22));
        uint8_t t26 = 0;
        F.ret = t25;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_lj2dhj3(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_start_lj2dhj3 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 178u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb2;
            {
                gm_b16_8 t5 = gm_l0;
                uint64_t t6 = gm_l1;
                uint64_t t7 = gm_f_ws_at_dchgqis(t5, t6);
                gm_l2 = t7;
            }
            {
                uint64_t t9 = gm_l2;
                uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
                if (!t10) goto ie8;
                {
                    goto lb2;
                }
                goto ix8;
ie8:;
ix8:;
            }
            {
                uint64_t t12 = gm_l1;
                uint64_t t13 = gm_l2;
                uint64_t r14; bool o15 = __builtin_add_overflow(t12, t13, &r14);
                if (o15) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 73, 9);
                uint64_t t16 = r14;
                gm_l1 = t16;
            }
lc2:;
        }
lb2:;
    }
    {
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t18 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t19 = gm_l1;
        uint64_t t20 = (uintptr_t)t19;
        if (t20 > (uintptr_t)t17 || (uintptr_t)t17 > (uintptr_t)t17) gm_trap_slice(t20, (uintptr_t)t17, t17, gm_src8pvk8vd1qnr1, 75, 12);
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(((uint8_t *)t18 + t20 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)((uintptr_t)t17 - t20));
        uint8_t t22 = 0;
        F.ret = t21;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unwrap_lines_59i2isg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_unwrap_lines_59i2isg F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 1));
                    uint8_t t14 = t13;
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t14));
                    gm_l4 = t17;
                    {
                        void * t19 = (void *)((uint8_t *)&F.l1);
                        uint8_t t20;
                        uint8_t t22 = gm_l4;
                        uint8_t t23 = ((t22 == ((uint8_t)10ULL)));
                        if (!t23) goto ie21;
                        uint8_t t25 = ((uint8_t)32ULL);
                        t20 = t25;
                        goto ix21;
ie21:;
                        uint8_t t27 = gm_l4;
                        uint8_t t28 = t27;
                        t20 = t28;
ix21:;
                        gm_tftb1odc739t5 t29 = gm_f_push_t83rg2m(t19, t20);
                        gm_tftb1odc739t5 t30 = t29;
                        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_src8pvk8vd1qnr1, 116, 18);
                        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 116, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t33 = gm_l3;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l3 = t36;
                }
            }
lb4:;
        }
    }
    {
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t38 = t37;
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_url_path_ul3c89a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_url_path_ul3c89a F;
    gm_b16_8 gm_l0; /* path */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* b */
    GM_STACK_CHECK(&F, 235u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 1));
                    uint8_t t14 = t13;
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t14));
                    gm_l4 = t17;
                    {
                        uint8_t t19 = gm_l4;
                        uint8_t t20 = t19;
                        uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if ((t25 < ((uint8_t)65ULL)) || (t25 > ((uint8_t)90ULL))) goto pa24;
                        goto po23;
pa24:;
                        uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if ((t27 < ((uint8_t)97ULL)) || (t27 > ((uint8_t)122ULL))) goto pa26;
                        goto po23;
pa26:;
                        uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if ((t29 < ((uint8_t)48ULL)) || (t29 > ((uint8_t)57ULL))) goto pa28;
                        goto po23;
pa28:;
                        uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t31 == ((uint8_t)45ULL))) goto pa30;
                        goto po23;
pa30:;
                        uint8_t t33 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t33 == ((uint8_t)46ULL))) goto pa32;
                        goto po23;
pa32:;
                        uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t35 == ((uint8_t)95ULL))) goto pa34;
                        goto po23;
pa34:;
                        uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t37 == ((uint8_t)126ULL))) goto pa36;
                        goto po23;
pa36:;
                        uint8_t t39 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t39 == ((uint8_t)47ULL))) goto pa38;
                        goto po23;
pa38:;
                        goto mn22;
po23:;
                        void * t40 = (void *)((uint8_t *)&F.l1);
                        uint8_t t41 = gm_l4;
                        gm_tftb1odc739t5 t42 = gm_f_push_t83rg2m(t40, t41);
                        gm_tftb1odc739t5 t43 = t42;
                        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_src8pvk8vd1qnr1, 142, 73);
                        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
                        goto me21;
mn22:;
                        void * t46 = (void *)((uint8_t *)&F.l1);
                        uint8_t t47 = gm_l4;
                        gm_t9qkh0rsooc8d t48;
                        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), t47);
                        gm_tftb1odc739t5 t49 = gm_f_write_h3ti3fb(t46, t48);
                        gm_tftb1odc739t5 t50 = t49;
                        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_src8pvk8vd1qnr1, 143, 18);
                        gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
                        goto me21;
mn45:;
                        gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 141, 9);
me21:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 140, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t53 = gm_l3;
                    uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                    uint64_t t56 = r54;
                    gm_l3 = t56;
                }
            }
lb4:;
        }
    }
    {
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t58 = t57;
        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
        uint8_t t60 = 0;
        F.ret = t59;
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
__attribute__((aligned(4))) uint64_t gm_f_ws_at_dchgqis(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ws_at_dchgqis F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint8_t gm_l2; /* c */
    uint8_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* b1 */
    uint8_t gm_l5; /* b2 */
    uint8_t gm_l6; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 451u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = t2;
        void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t3)));
        uint64_t t5 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t t6 = gm_l1;
        uint64_t t7 = (uintptr_t)t6;
        gm_tqs5p4n2tq19v t8;
        if (t7 < t5) {
        uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)t4 + t7 * 1));
        gm_tqs5p4n2tq19v t10;
        GM_ST(uint8_t, ((uint8_t *)&t10), t9);
        ((uint8_t *)&t10)[1] = 1;
            t8 = t10;
        } else {
        gm_tqs5p4n2tq19v t11;
        memset(&t11, 0, sizeof t11);
            t8 = t11;
        }
        gm_tqs5p4n2tq19v t12 = t8;
        uint8_t t13;
        if (((((uint8_t *)&t12))[1] != 0)) {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
            t13 = t14;
        } else {
            uint8_t t15 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
        gm_l2 = t13;
    }
    {
        uint8_t t17 = 0;
        {
            uint8_t t19 = gm_l2;
            gm_l3 = t19;
        }
        uint8_t t20 = 0;
        uint8_t t21 = gm_l3;
        uint8_t t22 = (((uint8_t)9ULL) <= t21);
        if (t22) {
            uint8_t t23 = gm_l3;
            uint8_t t24 = (t23 <= ((uint8_t)13ULL));
            t20 = t24;
        } else t20 = 0;
        uint8_t t25 = t20;
        if (!t25) {
            uint8_t t26 = gm_l2;
            uint8_t t27 = ((t26 == ((uint8_t)32ULL)));
            t17 = t27;
        } else t17 = 1;
        if (!t17) goto ie16;
        {
            uint8_t t29 = 0;
            F.ret = ((uint64_t)1ULL);
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        uint8_t t31 = gm_l2;
        uint8_t t32 = (t31 < ((uint8_t)194ULL));
        if (!t32) goto ie30;
        {
            uint8_t t34 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        gm_b16_8 t35 = gm_l0;
        gm_b16_8 t36 = t35;
        void * t37 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t36)));
        uint64_t t38 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t36) + 8);
        uint64_t t39 = gm_l1;
        uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
        if (o41) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 47, 17);
        uint64_t t42 = r40;
        uint64_t t43 = (uintptr_t)t42;
        gm_tqs5p4n2tq19v t44;
        if (t43 < t38) {
        uint8_t t45 = GM_LD(uint8_t, ((uint8_t *)t37 + t43 * 1));
        gm_tqs5p4n2tq19v t46;
        GM_ST(uint8_t, ((uint8_t *)&t46), t45);
        ((uint8_t *)&t46)[1] = 1;
            t44 = t46;
        } else {
        gm_tqs5p4n2tq19v t47;
        memset(&t47, 0, sizeof t47);
            t44 = t47;
        }
        gm_tqs5p4n2tq19v t48 = t44;
        uint8_t t49;
        if (((((uint8_t *)&t48))[1] != 0)) {
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
            t49 = t50;
        } else {
            uint8_t t51 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
        gm_l4 = t49;
    }
    {
        gm_b16_8 t52 = gm_l0;
        gm_b16_8 t53 = t52;
        void * t54 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t53)));
        uint64_t t55 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t53) + 8);
        uint64_t t56 = gm_l1;
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)2ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_src8pvk8vd1qnr1, 48, 17);
        uint64_t t59 = r57;
        uint64_t t60 = (uintptr_t)t59;
        gm_tqs5p4n2tq19v t61;
        if (t60 < t55) {
        uint8_t t62 = GM_LD(uint8_t, ((uint8_t *)t54 + t60 * 1));
        gm_tqs5p4n2tq19v t63;
        GM_ST(uint8_t, ((uint8_t *)&t63), t62);
        ((uint8_t *)&t63)[1] = 1;
            t61 = t63;
        } else {
        gm_tqs5p4n2tq19v t64;
        memset(&t64, 0, sizeof t64);
            t61 = t64;
        }
        gm_tqs5p4n2tq19v t65 = t61;
        uint8_t t66;
        if (((((uint8_t *)&t65))[1] != 0)) {
            uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&t65) + 0));
            t66 = t67;
        } else {
            t66 = ((uint8_t)0ULL);
        }
        gm_l5 = t66;
    }
    {
        uint8_t t69 = gm_l2;
        uint8_t t70 = ((t69 == ((uint8_t)194ULL)));
        if (!t70) goto ie68;
        {
            uint64_t t72;
            uint8_t t74 = gm_l4;
            gm_tqs5p4n2tq19v t75;
            GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)133ULL));
            GM_ST(uint8_t, ((uint8_t *)&t75 + 1), ((uint8_t)160ULL));
            gm_tqs5p4n2tq19v t76 = t75;
            uint8_t t77 = 0;
            uint8_t t78 = t74;
            if (gm_eqjmccqdb90f(((uint8_t *)&t76) + 0, ((uint8_t *)&t78))) t77 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t76) + 1, ((uint8_t *)&t78))) t77 = 1;
            uint8_t t79 = (uint8_t)(t77);
            if (!t79) goto ie73;
            uint64_t t81 = ((uint64_t)2ULL);
            t72 = t81;
            goto ix73;
ie73:;
            uint64_t t83 = ((uint64_t)0ULL);
            t72 = t83;
ix73:;
            uint8_t t84 = 0;
            F.ret = t72;
            goto gm_fret;
        }
        goto ix68;
ie68:;
ix68:;
    }
    {
        uint8_t t86 = gm_l2;
        uint8_t t87 = ((t86 == ((uint8_t)225ULL)));
        if (!t87) goto ie85;
        {
            uint64_t t89;
            uint8_t t91 = gm_l4;
            uint8_t t92 = ((t91 == ((uint8_t)154ULL)));
            if (!t92) goto ie90;
            uint8_t t93 = gm_l5;
            uint8_t t94 = ((t93 == ((uint8_t)128ULL)));
            if (!t94) goto ie90;
            uint64_t t96 = ((uint64_t)3ULL);
            t89 = t96;
            goto ix90;
ie90:;
            uint64_t t98 = ((uint64_t)0ULL);
            t89 = t98;
ix90:;
            uint8_t t99 = 0;
            F.ret = t89;
            goto gm_fret;
        }
        goto ix85;
ie85:;
ix85:;
    }
    {
        uint8_t t101 = gm_l2;
        uint8_t t102 = ((t101 == ((uint8_t)226ULL)));
        if (!t102) goto ie100;
        {
            uint8_t t105 = gm_l4;
            uint8_t t106 = ((t105 == ((uint8_t)128ULL)));
            if (!t106) goto ie104;
            uint8_t t107 = 0;
            {
                uint8_t t109 = gm_l5;
                gm_l6 = t109;
            }
            uint8_t t110 = 0;
            uint8_t t111 = gm_l6;
            uint8_t t112 = (((uint8_t)128ULL) <= t111);
            if (t112) {
                uint8_t t113 = gm_l6;
                uint8_t t114 = (t113 <= ((uint8_t)138ULL));
                t110 = t114;
            } else t110 = 0;
            uint8_t t115 = t110;
            if (!t115) {
                uint8_t t116 = gm_l5;
                gm_toh0ootodhjue t117;
                GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)168ULL));
                GM_ST(uint8_t, ((uint8_t *)&t117 + 1), ((uint8_t)169ULL));
                GM_ST(uint8_t, ((uint8_t *)&t117 + 2), ((uint8_t)175ULL));
                gm_toh0ootodhjue t118 = t117;
                uint8_t t119 = 0;
                uint8_t t120 = t116;
                if (gm_eqjmccqdb90f(((uint8_t *)&t118) + 0, ((uint8_t *)&t120))) t119 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t118) + 1, ((uint8_t *)&t120))) t119 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t118) + 2, ((uint8_t *)&t120))) t119 = 1;
                uint8_t t121 = (uint8_t)(t119);
                t107 = t121;
            } else t107 = 1;
            if (!t107) goto ie104;
            {
                uint8_t t123 = 0;
                F.ret = ((uint64_t)3ULL);
                goto gm_fret;
            }
            goto ix104;
ie104:;
ix104:;
        }
        {
            uint64_t t124;
            uint8_t t126 = gm_l4;
            uint8_t t127 = ((t126 == ((uint8_t)129ULL)));
            if (!t127) goto ie125;
            uint8_t t128 = gm_l5;
            uint8_t t129 = ((t128 == ((uint8_t)159ULL)));
            if (!t129) goto ie125;
            uint64_t t131 = ((uint64_t)3ULL);
            t124 = t131;
            goto ix125;
ie125:;
            uint64_t t133 = ((uint64_t)0ULL);
            t124 = t133;
ix125:;
            uint8_t t134 = 0;
            F.ret = t124;
            goto gm_fret;
        }
        goto ix100;
ie100:;
ix100:;
    }
    {
        uint64_t t135;
        uint8_t t137 = gm_l2;
        uint8_t t138 = ((t137 == ((uint8_t)227ULL)));
        if (!t138) goto ie136;
        uint8_t t139 = gm_l4;
        uint8_t t140 = ((t139 == ((uint8_t)128ULL)));
        if (!t140) goto ie136;
        uint8_t t141 = gm_l5;
        uint8_t t142 = ((t141 == ((uint8_t)128ULL)));
        if (!t142) goto ie136;
        uint64_t t144 = ((uint64_t)3ULL);
        t135 = t144;
        goto ix136;
ie136:;
        uint64_t t146 = ((uint64_t)0ULL);
        t135 = t146;
ix136:;
        uint8_t t147 = 0;
        F.ret = t135;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_ws_before_jst10le(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ws_before_jst10le F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* end */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_l3 = ((uint64_t)1ULL);
        }
        {
            gm_l2 = ((uint64_t)3ULL);
        }
        {
            uint64_t t3 = gm_l3;
            uint64_t t4 = gm_l2;
            uint8_t t5 = (t3 <= t4);
            gm_l4 = t5;
        }
        {
            for (;;) {
                uint8_t t7 = gm_l4;
                if (!t7) goto lb6;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = t10;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t17 = gm_l1;
                        uint64_t t18 = gm_l5;
                        uint8_t t19 = (t17 >= t18);
                        if (!t19) goto ie16;
                        gm_b16_8 t20 = gm_l0;
                        uint64_t t21 = gm_l1;
                        uint64_t t22 = gm_l5;
                        uint64_t r23; bool o24 = __builtin_sub_overflow(t21, t22, &r23);
                        if (o24) gm_trap("integer overflow in `-`", gm_src8pvk8vd1qnr1, 62, 34);
                        uint64_t t25 = r23;
                        uint64_t t26 = gm_f_ws_at_dchgqis(t20, t25);
                        uint64_t t27 = gm_l5;
                        uint8_t t28 = ((t26 == t27));
                        if (!t28) goto ie16;
                        {
                            uint64_t t30 = gm_l5;
                            uint8_t t31 = 0;
                            F.ret = t30;
                            goto gm_fret;
                        }
                        goto ix16;
ie16:;
ix16:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_src8pvk8vd1qnr1, 61, 5);
me12:;
                }
lc6:;
                uint64_t t33 = gm_l3;
                uint64_t t34 = gm_l2;
                uint8_t t35 = ((t33 == t34));
                if (!t35) goto ie32;
                {
                    gm_l4 = ((uint8_t)0);
                }
                goto ix32;
ie32:;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
ix32:;
            }
lb6:;
        }
    }
    {
        uint8_t t42 = 0;
        F.ret = ((uint64_t)0ULL);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
