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
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint8_t a[2]; } gm_t5liq177ejcou;
typedef struct { uint8_t a[32]; } gm_t7mlrlibdq1ja;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t a[8]; } gm_tg8e8rhc3qio9;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t a[64]; } gm_tsn02dhhitgs4;
typedef struct { gm_b16_8 a[23]; } gm_t0ut772p7ffln;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
_Static_assert(sizeof(gm_t5liq177ejcou) == 2 && _Alignof(gm_t5liq177ejcou) == 1 && offsetof(gm_t5liq177ejcou, a) == 0, "layout of `[2]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t7mlrlibdq1ja) == 32 && _Alignof(gm_t7mlrlibdq1ja) == 1 && offsetof(gm_t7mlrlibdq1ja, a) == 0, "layout of `[32]u8`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_b16_8 a[7]; } gm_tdv3dkjvo1l7t;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tg8e8rhc3qio9) == 32 && _Alignof(gm_tg8e8rhc3qio9) == 4 && offsetof(gm_tg8e8rhc3qio9, a) == 0, "layout of `[8]u32`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tipro5npl6rd8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_tg8e8rhc3qio9 f0; gm_tsn02dhhitgs4 f1; uint64_t f2; uint64_t f3; } gm_tq14qvifgdvpm;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsn02dhhitgs4) == 64 && _Alignof(gm_tsn02dhhitgs4) == 1 && offsetof(gm_tsn02dhhitgs4, a) == 0, "layout of `[64]u8`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_t0ut772p7ffln) == 368 && _Alignof(gm_t0ut772p7ffln) == 8 && offsetof(gm_t0ut772p7ffln, a) == 0, "layout of `[23]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t6j6smj7qoctu v1; } f1; } gm_t22b1ebhvur5a;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tdv3dkjvo1l7t) == 112 && _Alignof(gm_tdv3dkjvo1l7t) == 8 && offsetof(gm_tdv3dkjvo1l7t, a) == 0, "layout of `[7]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tibupt4dmcqai v1; } f1; } gm_te2n4j2vtm53d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tipro5npl6rd8) == 40 && _Alignof(gm_tipro5npl6rd8) == 8 && offsetof(gm_tipro5npl6rd8, f0) == 0 && offsetof(gm_tipro5npl6rd8, f1) == 4 && offsetof(gm_tipro5npl6rd8, f2) == 8 && offsetof(gm_tipro5npl6rd8, f3) == 24, "layout of `Ran`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tq14qvifgdvpm) == 112 && _Alignof(gm_tq14qvifgdvpm) == 8 && offsetof(gm_tq14qvifgdvpm, f0) == 0 && offsetof(gm_tq14qvifgdvpm, f1) == 32 && offsetof(gm_tq14qvifgdvpm, f2) == 96 && offsetof(gm_tq14qvifgdvpm, f3) == 104, "layout of `Sha256`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_t22b1ebhvur5a) == 40 && _Alignof(gm_t22b1ebhvur5a) == 8 && offsetof(gm_t22b1ebhvur5a, f0) == 0 && offsetof(gm_t22b1ebhvur5a, f1) == 8, "layout of `string!(ident: string, rest: string)`");
_Static_assert(sizeof(gm_te2n4j2vtm53d) == 32 && _Alignof(gm_te2n4j2vtm53d) == 8 && offsetof(gm_te2n4j2vtm53d, f0) == 0 && offsetof(gm_te2n4j2vtm53d, f1) == 8, "layout of `IoError!Entry`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
struct gm_fr_discard_bdpju80 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_download_eek737p {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* args */
    gm_t0ut772p7ffln l5; /* <tmp> */
    gm_tipro5npl6rd8 l9; /* r */
    gm_t22b1ebhvur5a ret;
};
struct gm_fr_file_sha256_llree8t {
    gm_frame h;
    gm_tq14qvifgdvpm l3; /* h */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_for_body_1avb45k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_gp0er0a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_nnvr1n3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_tv3u4f0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_leaves_directory_kq5llq4 {
    gm_frame h;
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_place_snpjvsu {
    gm_frame h;
    gm_b16_8 l0; /* tmp */
    gm_trqfvmjtt4sij l2; /* names */
    gm_tl572d7579hns ret;
    gm_b0_1 w0;
};
struct gm_fr_unpack_eb2aboi {
    gm_frame h;
    gm_tl572d7579hns ret;
};
struct gm_fr_unpack_into_kqij1d2 {
    gm_frame h;
    gm_t9bmh80edou9c l6; /* names_args */
    gm_tipro5npl6rd8 l7; /* names_run */
    gm_t9bmh80edou9c l8; /* types_args */
    gm_tipro5npl6rd8 l9; /* types_run */
    gm_trqfvmjtt4sij l10; /* names */
    gm_trqfvmjtt4sij l12; /* types */
    gm_tdv3dkjvo1l7t l21; /* args */
    gm_tipro5npl6rd8 l22; /* r */
    gm_tl572d7579hns ret;
    gm_b0_1 w0;
    gm_b0_1 w1;
};
bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_discard_bdpju80(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_download_eek737p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_s40i17b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_file_sha256_llree8t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_final_rg5ko3p(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1avb45k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_gp0er0a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_nnvr1n3(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tv3u4f0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_04pispc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0rhberc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5pl0mkc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5uun4vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8q6hdku(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ee96hnp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ep7kj4f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
__attribute__((aligned(4))) void gm_f_format_l0gkulr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_m1bjvb9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
__attribute__((aligned(4))) void gm_f_format_nk3ghj0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_hex_euih242(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_init_s32ien0(void *gm_out);
__attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_leaves_directory_kq5llq4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s84ppfm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s9kjq5m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) void gm_f_make_dirs_gbp8nfr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parent_4i83qet(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_place_snpjvsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_private_name_83ij38e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_all_h0k1qkh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_run_mj5dobu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_sanitize_modes_mvteloh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s8m2rsv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_unpack_eb2aboi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_unpack_into_kqij1d2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_update_pn3fq6a(void * a0, gm_b16_8 a1);
extern void *const gm_hlh176qr0nti[];
extern void *const gm_hlhtabotcjhg[];
extern void *const gm_hlq1reae6pf9[];
extern void *const gm_hlucu919n5n4[];
extern uint8_t gm_k0kae0be6jj[40] __attribute__((aligned(8)));
extern uint8_t gm_kh1ssdqq46n[40] __attribute__((aligned(8)));
extern uint8_t gm_kr7fqi9m0ih[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0c2823k4ul3o[17];
extern const uint8_t gm_s104g112fsi3t[3];
extern const uint8_t gm_s1ktljc2oac64[4];
extern const uint8_t gm_s24qpbg6rctcd[17];
extern const uint8_t gm_s2dssfe3261t2[2];
extern const uint8_t gm_s4ufkrqv2ag7g[7];
extern const uint8_t gm_s5v7o1bkddp5s[7];
extern const uint8_t gm_s7cn098jq5gvg[9];
extern const uint8_t gm_s8nekj984lthm[15];
extern const uint8_t gm_s9pknf9g05okt[3];
extern const uint8_t gm_sa5v7oq7un6c6[84];
extern const uint8_t gm_sarlhchtvv77m[13];
extern const uint8_t gm_sasgv00j2p5nu[18];
extern const uint8_t gm_sbtoph8phkseu[5];
extern const uint8_t gm_sdaknqn35hokt[9];
extern const uint8_t gm_sfpeqbitcbq42[6];
extern const uint8_t gm_sg91clgj6l88m[6];
extern const uint8_t gm_sgs65cm0mtgm4[14];
extern const uint8_t gm_skpl66e1qvhab[8];
extern const uint8_t gm_slahfmk66v1v4[15];
extern const uint8_t gm_slqtudd4gk04i[9];
extern const uint8_t gm_smaguor10so8u[12];
extern const uint8_t gm_snauhge6bheis[14];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snhr2rebnnkc2[22];
extern const uint8_t gm_sobnp9i68hvdj[16];
extern const uint8_t gm_sos62dipmhb8k[6];
extern const uint8_t gm_sppohbjp90mol[3];
extern const uint8_t gm_sqviis18fpvj7[5];
extern const char gm_src3s76fqo1a9jk[];
extern const uint8_t gm_srrq1irp1jfep[12];
extern const uint8_t gm_ss6cscjfm4jfc[11];
extern const uint8_t gm_ss9vj0o06n3e8[8];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_stjlu12rqiie7[13];
extern const uint8_t gm_suq2iqmhi931e[3];
extern const uint8_t gm_svubtddeidu1d[2];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_b0_1 gm_f_discard_bdpju80(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_discard_bdpju80 F;
    gm_t6j6smj7qoctu gm_l0; /* d */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 0));
            gm_ttln0d0hd70en t4 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
__attribute__((aligned(4))) void gm_f_download_eek737p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_download_eek737p F;
    gm_b16_8 gm_l0; /* url */
    gm_b16_8 gm_l1; /* dir */
    gm_b16_8 gm_l2; /* path */
    gm_b16_8 gm_l3; /* max */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* a */
    gm_b16_8 gm_l10; /* e */
    gm_b16_8 gm_l11; /* sum */
    GM_STACK_CHECK(&F, 1889u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        gm_tl572d7579hns t3;
        gm_f_make_dirs_gbp8nfr((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl572d7579hns t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
            gm_t22b1ebhvur5a t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t6 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t6 + 32, 0);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
    }
    {
        gm_b16_8 t8 = gm_l1;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sdaknqn35hokt));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(8));
        gm_b16_8 t10 = gm_f_private_name_83ij38e((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t10;
    }
    {
        gm_t19enj90vg8bd t11;
        GM_ST(int32_t, ((uint8_t *)&t11 + 0), ((int32_t)1073741824LL));
        gm_tabb710d9mq2e t12;
        gm_f_format_m1bjvb9((void *)&t12, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_src3s76fqo1a9jk, 21, 12);
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
        gm_l3 = t14;
    }
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            {
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sbtoph8phkseu));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 0, t17);
                gm_b16_8 t18;
                GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s4ufkrqv2ag7g));
                GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(6));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 16, t18);
                gm_b16_8 t19;
                GM_ST(void *, (uint8_t *)&t19, (void *)(gm_slqtudd4gk04i));
                GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(8));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 32, t19);
                gm_b16_8 t20;
                GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sarlhchtvv77m));
                GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(12));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 48, t20);
                gm_b16_8 t21;
                GM_ST(void *, (uint8_t *)&t21, (void *)(gm_ss6cscjfm4jfc));
                GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(10));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 64, t21);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(gm_skpl66e1qvhab));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(7));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 80, t22);
                gm_b16_8 t23;
                GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s2dssfe3261t2));
                GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 96, t23);
                gm_b16_8 t24;
                GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sasgv00j2p5nu));
                GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(17));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 112, t24);
                gm_b16_8 t25;
                GM_ST(void *, (uint8_t *)&t25, (void *)(gm_suq2iqmhi931e));
                GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(2));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 128, t25);
                gm_b16_8 t26;
                GM_ST(void *, (uint8_t *)&t26, (void *)(gm_snauhge6bheis));
                GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(13));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 144, t26);
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(gm_svubtddeidu1d));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(1));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 160, t27);
                gm_b16_8 t28;
                GM_ST(void *, (uint8_t *)&t28, (void *)(gm_stjlu12rqiie7));
                GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(12));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 176, t28);
                gm_b16_8 t29;
                GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s104g112fsi3t));
                GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(2));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 192, t29);
                gm_b16_8 t30;
                GM_ST(void *, (uint8_t *)&t30, (void *)(gm_slahfmk66v1v4));
                GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(14));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 208, t30);
                gm_b16_8 t31 = gm_l3;
                gm_b16_8 t32 = t31;
                gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 224, t33);
                gm_b16_8 t34;
                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_ss9vj0o06n3e8));
                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(7));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 240, t34);
                gm_b16_8 t35;
                GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s24qpbg6rctcd));
                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(16));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 256, t35);
                gm_b16_8 t36;
                GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sgs65cm0mtgm4));
                GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(13));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 272, t36);
                gm_b16_8 t37;
                GM_ST(void *, (uint8_t *)&t37, (void *)(gm_smaguor10so8u));
                GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(11));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 288, t37);
                gm_b16_8 t38;
                GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s7cn098jq5gvg));
                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(8));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 304, t38);
                gm_b16_8 t39 = gm_l2;
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 320, t39);
                gm_b16_8 t40;
                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s9pknf9g05okt));
                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(2));
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 336, t40);
                gm_b16_8 t41 = gm_l0;
                GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 352, t41);
            }
            void * t42 = ((uint8_t *)&F.l5);
            if ((uintptr_t)0 > (uintptr_t)23 || (uintptr_t)23 > (uintptr_t)23) gm_trap_slice((uintptr_t)0, (uintptr_t)23, 23, gm_src3s76fqo1a9jk, 23, 5);
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t42 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)((uintptr_t)23 - (uintptr_t)0));
            gm_b16_8 t44 = t43;
            gm_l6 = t44;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t46 = gm_l7;
                uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t48 = (t46 < t47);
                if (!t48) goto lb45;
                {
                    uint64_t t51 = gm_l7;
                    uint64_t t52 = (uintptr_t)t51;
                    uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t52 * 16));
                    gm_l8 = t56;
                    {
                        void * t58 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t59 = gm_l8;
                        gm_tftb1odc739t5 t60 = gm_f_push_3ef746b(t58, t59);
                        gm_tftb1odc739t5 t61 = t60;
                        if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_src3s76fqo1a9jk, 30, 9);
                        gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
                    }
                    goto me54;
mn55:;
                    gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 23, 5);
me54:;
                }
lc45:;
                {
                    uint64_t t64 = gm_l7;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    uint64_t t67 = r65;
                    gm_l7 = t67;
                }
            }
lb45:;
        }
    }
    {
        gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t69 = t68;
        gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
        gm_f_run_mj5dobu((void *)&F.l9, (void *)NULL, cx, NULL, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t72 = GM_LD(uint32_t, (((uint8_t *)&F.l9) + 4));
        uint8_t t73 = ((t72 == ((uint32_t)127ULL)));
        if (!t73) goto ie71;
        uint64_t t74 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 24) + 8);
        uint8_t t75 = (t74 > ((uint64_t)0ULL));
        if (!t75) goto ie71;
        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
        uint8_t t77 = (uint8_t)!t76;
        if (!t77) goto ie71;
        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 24));
        gm_b16_8 t79;
        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sg91clgj6l88m));
        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(5));
        uint8_t t80 = gm_f_contains_gkki4kq(t78, t79);
        uint8_t t81 = (uint8_t)!t80;
        if (!t81) goto ie71;
        {
            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 24));
            gm_tnpifv20irnph t84;
            GM_ST(gm_b16_8, ((uint8_t *)&t84 + 0), t83);
            gm_tabb710d9mq2e t85;
            gm_f_format_ee96hnp((void *)&t85, (void *)NULL, cx, NULL, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t86 = t85;
            if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 8, gm_src3s76fqo1a9jk, 33, 17);
            gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
            gm_b16_8 t88 = t87;
            gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
            gm_t22b1ebhvur5a t90;
            GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t90 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t90 + 32, 0);
            ((uint8_t *)&t90)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t90 + 8), t89);
            gm_t22b1ebhvur5a t91 = t90;
            uint8_t t92 = ((((uint8_t *)&t91))[0] != 0);
            F.ret = t90;
            goto gm_fret;
        }
        goto ix71;
ie71:;
ix71:;
    }
    {
        uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
        uint8_t t95 = (uint8_t)!t94;
        if (!t95) goto ie93;
        {
            {
                gm_b16_8 t98 = gm_l2;
                gm_ttln0d0hd70en t99 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t98);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            gm_t6j6smj7qoctu t100;
            gm_b16_8 t101 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t100 + 0, t101);
            gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 24));
            gm_b16_8 t103 = gm_f_trim_fdrfe6i(t102);
            GM_ST(gm_b16_8, (uint8_t *)&t100 + 16, t103);
            gm_tabb710d9mq2e t104;
            gm_f_format_l0gkulr((void *)&t104, (void *)NULL, cx, NULL, &t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t105 = t104;
            if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 8, gm_src3s76fqo1a9jk, 37, 17);
            gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&t105) + 8));
            gm_b16_8 t107 = t106;
            gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
            gm_t22b1ebhvur5a t109;
            GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t109 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t109 + 32, 0);
            ((uint8_t *)&t109)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t109 + 8), t108);
            gm_t22b1ebhvur5a t110 = t109;
            uint8_t t111 = ((((uint8_t *)&t110))[0] != 0);
            F.ret = t109;
            goto gm_fret;
        }
        goto ix93;
ie93:;
ix93:;
    }
    {
        gm_b16_8 t112 = gm_l2;
        gm_t3ujhu6qph7de t113;
        gm_f_file_sha256_llree8t((void *)&t113, (void *)NULL, cx, NULL, t112);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ujhu6qph7de t114 = t113;
        gm_b16_8 t115;
        if (!((((uint8_t *)&t114))[0] != 0)) {
            gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
            t115 = t116;
        } else {
            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
            gm_l10 = t117;
            {
                {
                    gm_b16_8 t120 = gm_l2;
                    gm_ttln0d0hd70en t121 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t120);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            gm_b16_8 t122 = gm_l10;
            gm_t22b1ebhvur5a t123;
            GM_ST(uint8_t, (uint8_t *)&t123 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t123 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t123 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t123 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t123 + 32, 0);
            ((uint8_t *)&t123)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t123 + 8), t122);
            gm_t22b1ebhvur5a t124 = t123;
            uint8_t t125 = ((((uint8_t *)&t124))[0] != 0);
            F.ret = t123;
            goto gm_fret;
        }
        gm_l11 = t115;
    }
    {
        gm_t6j6smj7qoctu t126;
        gm_b16_8 t127 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t126 + 0, t127);
        gm_b16_8 t128 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t126 + 16, t128);
        gm_t22b1ebhvur5a t129;
        GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
        ((uint8_t *)&t129)[0] = 0;
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t129 + 8), t126);
        gm_t22b1ebhvur5a t130 = t129;
        uint8_t t131 = ((((uint8_t *)&t130))[0] != 0);
        F.ret = t129;
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
__attribute__((aligned(4))) void gm_f_file_sha256_llree8t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_file_sha256_llree8t F;
    gm_b16_8 gm_l0; /* path */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* f */
    gm_b16_8 gm_l4; /* buf */
    gm_toe5fmr3vvf4d gm_l5; /* e */
    uint64_t gm_l6; /* n */
    gm_t7mlrlibdq1ja gm_l7; /* d */
    GM_STACK_CHECK(&F, 1162u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_t4q2b5ibsrdik t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 3), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 4), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 5), ((uint8_t)0));
        gm_node *pn4 = gm_find(cx, 2126215853);
        if (!pn4) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 53, 10);
        gm_t62ebbi2i5rkd t5;
        t5 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn4->clauses[0])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t62ebbi2i5rkd t6 = t5;
        gm_tm98ap4112hee t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            gm_tm98ap4112hee t8 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t6) + 8));
            t7 = t8;
        } else {
            gm_toe5fmr3vvf4d t9 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t6) + 8));
            gm_l1 = t9;
            gm_t6j6smj7qoctu t11;
            gm_b16_8 t12 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 0, t12);
            gm_toe5fmr3vvf4d t13 = gm_l1;
            gm_b16_8 t14 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 16, t14);
            gm_tabb710d9mq2e t15;
            gm_f_format_04pispc((void *)&t15, (void *)NULL, cx, NULL, &t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t16 = t15;
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_src3s76fqo1a9jk, 53, 62);
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            gm_b16_8 t18 = t17;
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            gm_t3ujhu6qph7de t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
            ((uint8_t *)&t20)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t20 + 8), t19);
            gm_t3ujhu6qph7de t21 = t20;
            uint8_t t22 = ((((uint8_t *)&t21))[0] != 0);
            F.ret = t20;
            goto gm_fret;
        }
        gm_l2 = t7;
    }
    {
    }
    {
        gm_f_init_s32ien0((void *)&F.l3);
    }
    {
        unsigned __int128 n26 = (unsigned __int128)((uint64_t)16ULL);
        if (n26 >= 64) gm_trap_shift((uint64_t)n26, "usize", gm_src3s76fqo1a9jk, 56, 21);
        uint64_t r27 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n26);
        bool o28 = (uint64_t)(r27 >> (int)n26) != ((uint64_t)1ULL);
        if (o28) gm_trap("integer overflow in `<<`", gm_src3s76fqo1a9jk, 56, 21);
        uint64_t t29 = r27;
        gm_tabb710d9mq2e t30;
        gm_f_make_2ip78bj((void *)&t30, (void *)NULL, cx, NULL, t29, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kh1ssdqq46n)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_tabb710d9mq2e t31 = t30;
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_src3s76fqo1a9jk, 56, 12);
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
        gm_l4 = t32;
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb37;
            {
                gm_tm98ap4112hee t40 = gm_l2;
                gm_b16_8 t41 = gm_l4;
                gm_node *pn42 = gm_find(cx, 2126215853);
                if (!pn42) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 59, 14);
                gm_thf5kr0is9u9i t43;
                t43 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn42->clauses[2])(pn42->env, pn42->hctx, pn42, t40, t41);
                if (gm_unw.kind) {
                    goto gm_ll38;
                }
                gm_thf5kr0is9u9i t44 = t43;
                uint64_t t45;
                if (!((((uint8_t *)&t44))[0] != 0)) {
                    uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&t44) + 8));
                    t45 = t46;
                } else {
                    gm_toe5fmr3vvf4d t47 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t44) + 8));
                    gm_l5 = t47;
                    gm_t6j6smj7qoctu t49;
                    gm_b16_8 t50 = gm_l0;
                    GM_ST(gm_b16_8, (uint8_t *)&t49 + 0, t50);
                    gm_toe5fmr3vvf4d t51 = gm_l5;
                    gm_b16_8 t52 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t51);
                    if (gm_unw.kind) {
                        goto gm_ll38;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t49 + 16, t52);
                    gm_tabb710d9mq2e t53;
                    gm_f_format_04pispc((void *)&t53, (void *)NULL, cx, NULL, &t49);
                    if (gm_unw.kind) {
                        goto gm_ll38;
                    }
                    gm_tabb710d9mq2e t54 = t53;
                    if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 8, gm_src3s76fqo1a9jk, 59, 46);
                    gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
                    gm_b16_8 t56 = t55;
                    gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                    gm_t3ujhu6qph7de t58;
                    GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                    ((uint8_t *)&t58)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t58 + 8), t57);
                    gm_t3ujhu6qph7de t59 = t58;
                    uint8_t t60 = ((((uint8_t *)&t59))[0] != 0);
                    F.ret = t58;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t60;
                    goto gm_ll38;
                }
                gm_l6 = t45;
            }
            {
                uint64_t t62 = gm_l6;
                uint8_t t63 = ((t62 == ((uint64_t)0ULL)));
                if (!t63) goto ie61;
                {
                    goto lb37;
                }
                goto ix61;
ie61:;
ix61:;
            }
            {
                void * t65 = (void *)((uint8_t *)&F.l3);
                uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                void * t67 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                uint64_t t68 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t69 = gm_l6;
                uint64_t t70 = (uintptr_t)t69;
                if (t68 > t70 || t70 > (uintptr_t)t66) gm_trap_slice(t68, t70, t66, gm_src3s76fqo1a9jk, 61, 18);
                gm_b16_8 t71;
                GM_ST(void *, (uint8_t *)&t71, (void *)(((uint8_t *)t67 + t68 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(t70 - t68));
                gm_b16_8 t72 = t71;
                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                gm_f_update_pn3fq6a(t65, t73);
            }
lc37:;
        }
gm_ll38:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb37; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc37; }
        }
        goto gm_ld1_2;
lb37:;
    }
    {
        void * t74 = (void *)((uint8_t *)&F.l3);
        gm_f_final_rg5ko3p((void *)&gm_l7, t74);
    }
    {
        void * t75 = ((uint8_t *)&gm_l7);
        if ((uintptr_t)0 > (uintptr_t)32 || (uintptr_t)32 > (uintptr_t)32) gm_trap_slice((uintptr_t)0, (uintptr_t)32, 32, gm_src3s76fqo1a9jk, 64, 16);
        gm_b16_8 t76;
        GM_ST(void *, (uint8_t *)&t76, (void *)(((uint8_t *)t75 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)((uintptr_t)32 - (uintptr_t)0));
        gm_b16_8 t77 = t76;
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
        gm_b16_8 t79 = gm_f_hex_euih242((void *)NULL, cx, NULL, t78);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
        gm_t3ujhu6qph7de t80;
        GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
        ((uint8_t *)&t80)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t80 + 8), t79);
        gm_t3ujhu6qph7de t81 = t80;
        uint8_t t82 = ((((uint8_t *)&t81))[0] != 0);
        F.ret = t80;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t82;
        goto gm_ld1_2;
    }
    {
        gm_b16_8 t83 = gm_l4;
        gm_b16_8 t84 = t83;
        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
        gm_b0_1 t86 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t85, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0kae0be6jj)));
    }
    {
        gm_tm98ap4112hee t87 = gm_l2;
        gm_node *pn88 = gm_find(cx, 2126215853);
        if (!pn88) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 54, 11);
        gm_b0_1 t89;
        t89 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn88->clauses[1])(pn88->env, pn88->hctx, pn88, t87);
    }
    goto gm_ls90;
gm_ld1_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_b16_8 t91 = gm_l4;
        gm_b16_8 t92 = t91;
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
        gm_b0_1 t94 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t93, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0kae0be6jj)));
        gm_unw = gm_saved;
    }
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t95 = gm_l2;
        gm_node *pn96 = gm_find(cx, 2126215853);
        if (!pn96) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 54, 11);
        gm_b0_1 t97;
        t97 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn96->clauses[1])(pn96->env, pn96->hctx, pn96, t95);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls90:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1avb45k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_1avb45k F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK(&F, 50u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint8_t t6 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_snenkhfev166s, 2) == 0));
        if (!t6) goto ie5;
        {
            uint8_t t8 = 0;
            ((struct gm_fr_leaves_directory_kq5llq4 *)((&F.h)->up))->ret = ((uint8_t)1);
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t8;
            goto gm_lad;
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 131, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_gp0er0a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_gp0er0a F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    GM_STACK_CHECK(&F, 127u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_unpack_into_kqij1d2 *)((&F.h)->up))->l12);
        gm_b16_8 t6 = gm_l1;
        gm_b16_8 t7 = t6;
        void * t8 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t7)));
        uint64_t t9 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t7) + 8);
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        gm_tqs5p4n2tq19v t11;
        if (t10 < t9) {
        uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)t8 + t10 * 1));
        gm_tqs5p4n2tq19v t13;
        GM_ST(uint8_t, ((uint8_t *)&t13), t12);
        ((uint8_t *)&t13)[1] = 1;
            t11 = t13;
        } else {
        gm_tqs5p4n2tq19v t14;
        memset(&t14, 0, sizeof t14);
            t11 = t14;
        }
        gm_tqs5p4n2tq19v t15 = t11;
        uint8_t t16;
        if (((((uint8_t *)&t15))[1] != 0)) {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
            t16 = t17;
        } else {
            t16 = ((uint8_t)45ULL);
        }
        gm_tftb1odc739t5 t18 = gm_f_push_t83rg2m(t5, t16);
        gm_tftb1odc739t5 t19 = t18;
        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 1, gm_src3s76fqo1a9jk, 104, 44);
        gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 104, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_nnvr1n3(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_nnvr1n3 F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_toe5fmr3vvf4d gm_l2; /* err */
    gm_tibupt4dmcqai gm_l3; /* e */
    GM_STACK_CHECK(&F, 589u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_te2n4j2vtm53d t3 = GM_LD(gm_te2n4j2vtm53d, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_te2n4j2vtm53d t5 = gm_l1;
        gm_te2n4j2vtm53d t6 = t5;
        gm_tibupt4dmcqai t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            gm_tibupt4dmcqai t8 = GM_LD(gm_tibupt4dmcqai, (((uint8_t *)&t6) + 8));
            t7 = t8;
        } else {
            gm_toe5fmr3vvf4d t9 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t6) + 8));
            gm_l2 = t9;
            gm_t6j6smj7qoctu t11;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_place_snpjvsu *)((&F.h)->up))->l0));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 0, t12);
            gm_toe5fmr3vvf4d t13 = gm_l2;
            gm_b16_8 t14 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 16, t14);
            gm_tabb710d9mq2e t15;
            gm_f_format_04pispc((void *)&t15, (void *)NULL, cx, NULL, &t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t16 = t15;
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_src3s76fqo1a9jk, 141, 38);
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            gm_b16_8 t18 = t17;
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            gm_tl572d7579hns t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
            ((uint8_t *)&t20)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t20 + 8), t19);
            gm_tl572d7579hns t21 = t20;
            uint8_t t22 = ((((uint8_t *)&t21))[0] != 0);
            ((struct gm_fr_place_snpjvsu *)((&F.h)->up))->ret = t20;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t22;
            goto gm_lad;
        }
        gm_l3 = t7;
    }
    {
        void * t23 = (void *)((uint8_t *)&((struct gm_fr_place_snpjvsu *)((&F.h)->up))->l2);
        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
        gm_tabb710d9mq2e t25;
        gm_f_dupe_g9iqpvn((void *)&t25, (void *)NULL, cx, NULL, t24, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kr7fqi9m0ih)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t26 = t25;
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_src3s76fqo1a9jk, 142, 20);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
        gm_b16_8 t28 = t27;
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        gm_tftb1odc739t5 t30 = gm_f_push_3ef746b(t23, t29);
        gm_tftb1odc739t5 t31 = t30;
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_src3s76fqo1a9jk, 142, 9);
        gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 140, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tv3u4f0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_tv3u4f0 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_unpack_into_kqij1d2 *)((&F.h)->up))->l10);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_src3s76fqo1a9jk, 102, 44);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 102, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_leaves_directory_kq5llq4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_leaves_directory_kq5llq4 F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* <tmp> */
    GM_STACK_CHECK(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
        uint8_t t5 = gm_f_starts_with_6vvt01e(t3, t4);
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_b16_8 t9 = gm_l0;
            gm_l1 = t9;
        }
        gm_hval t10 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn11 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlucu919n5n4, .env = t10.env, .hctx = t10.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t14 = gm_l1;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(1));
        gm_b0_1 t16 = gm_f_split_obtinjv_s8m2rsv((void *)NULL, (&wn11), NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_ll13;
        }
        F.w0 = t16;
        goto gm_ls17;
gm_ll13:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we12; }
        goto gm_lad;
gm_ls17:;
we12:;
        gm_b0_1 t18 = F.w0;
        gm_b0_1 t19 = t18;
    }
    {
        uint8_t t20 = 0;
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
inline __attribute__((aligned(4))) void gm_f_place_snpjvsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_place_snpjvsu F;
    gm_b16_8 gm_l1; /* dest */
    gm_b16_8 gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* src */
    gm_b16_8 gm_l5; /* one */
    gm_t5j14m8ru132p gm_l6; /* info */
    gm_toe5fmr3vvf4d gm_l7; /* e */
    GM_STACK_CHECK(&F, 734u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = F.l0;
            gm_l3 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hlh176qr0nti, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l3;
        gm_b0_1 t9 = gm_f_entries_44o5n9f_s40i17b((void *)NULL, (&wn5), NULL, t8);
        if (gm_unw.kind) {
            goto gm_ll7;
        }
        F.w0 = t9;
        goto gm_ls10;
gm_ll7:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we6; }
        goto gm_lad;
gm_ls10:;
we6:;
        gm_b0_1 t11 = F.w0;
        gm_b0_1 t12 = t11;
    }
    {
        gm_b16_8 t13 = F.l0;
        gm_l4 = t13;
    }
    {
        uint64_t t15 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        uint8_t t16 = ((t15 == ((uint64_t)1ULL)));
        if (!t16) goto ie14;
        {
            gm_b16_8 t18 = F.l0;
            uint64_t t19 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_src3s76fqo1a9jk, 146, 26);
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t19 * 16));
            gm_b16_8 t22 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t18, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t22;
        }
        {
            gm_b16_8 t24 = gm_l5;
            gm_node *pn25 = gm_find(cx, 2126215853);
            if (!pn25) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 147, 20);
            gm_tih6h4avv5nrb t26;
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn25->clauses[13])((void *)&t26, pn25->env, pn25->hctx, pn25, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tih6h4avv5nrb t27 = t26;
            if (((((uint8_t *)&t27))[0] != 0)) goto ie23;
            gm_t5j14m8ru132p t28 = GM_LD(gm_t5j14m8ru132p, (((uint8_t *)&t27) + 8));
            gm_l6 = t28;
            {
                uint8_t t31 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 8) + 0) == ((uint8_t)1ULL)));
                if (!t31) goto ie30;
                {
                    gm_b16_8 t33 = gm_l5;
                    gm_l4 = t33;
                }
                goto ix30;
ie30:;
ix30:;
            }
            goto ix23;
ie23:;
ix23:;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        gm_b16_8 t34 = gm_l4;
        gm_b16_8 t35 = gm_l1;
        gm_node *pn36 = gm_find(cx, 2126215853);
        if (!pn36) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 151, 5);
        gm_ttln0d0hd70en t37;
        t37 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn36->clauses[7])(pn36->env, pn36->hctx, pn36, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttln0d0hd70en t38 = t37;
        gm_b0_1 t39;
        if (!((((uint8_t *)&t38))[0] != 0)) {
            gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 4));
            t39 = t40;
        } else {
            gm_toe5fmr3vvf4d t41 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t38) + 4));
            gm_l7 = t41;
            gm_b16_8 t44 = gm_l1;
            uint8_t t45 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t46 = (uint8_t)!t45;
            if (!t46) goto ie43;
            {
                gm_t6j6smj7qoctu t48;
                gm_b16_8 t49 = gm_l1;
                GM_ST(gm_b16_8, (uint8_t *)&t48 + 0, t49);
                gm_toe5fmr3vvf4d t50 = gm_l7;
                gm_b16_8 t51 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t48 + 16, t51);
                gm_tabb710d9mq2e t52;
                gm_f_format_5uun4vl((void *)&t52, (void *)NULL, cx, NULL, &t48);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t53 = t52;
                if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_src3s76fqo1a9jk, 153, 39);
                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
                gm_b16_8 t55 = t54;
                gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                gm_tl572d7579hns t57;
                GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                ((uint8_t *)&t57)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t57 + 8), t56);
                gm_tl572d7579hns t58 = t57;
                uint8_t t59 = ((((uint8_t *)&t58))[0] != 0);
                F.ret = t57;
                goto gm_fret;
            }
            goto ix43;
ie43:;
ix43:;
            gm_b0_1 t60 = GM_ZERO(gm_b0_1);
            t39 = t60;
        }
    }
    {
        gm_b0_1 t61;
        gm_tl572d7579hns t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t62 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t62 + 16, 0);
        ((uint8_t *)&t62)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t62 + 8), t61);
        gm_tl572d7579hns t63 = t62;
        uint8_t t64 = ((((uint8_t *)&t63))[0] != 0);
        F.ret = t62;
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
inline __attribute__((aligned(4))) void gm_f_unpack_eb2aboi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_unpack_eb2aboi F;
    gm_b16_8 gm_l0; /* archive */
    gm_b16_8 gm_l1; /* dest */
    gm_b16_8 gm_l2; /* parent_dir */
    gm_b16_8 gm_l3; /* tmp */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_tl572d7579hns gm_l5; /* result */
    GM_STACK_CHECK(&F, 767u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        gm_th2lqbo77pvsc t3;
        gm_f_parent_4i83qet((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t4 = t3;
        gm_b16_8 t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s0c2823k4ul3o));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(16));
            gm_tl572d7579hns t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t8 + 8), t7);
            gm_tl572d7579hns t9 = t8;
            uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        gm_b16_8 t11 = gm_l2;
        gm_tl572d7579hns t12;
        gm_f_make_dirs_gbp8nfr((void *)&t12, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl572d7579hns t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) {
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
            gm_tl572d7579hns t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 8), t14);
            F.ret = t15;
            goto gm_fret;
        }
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 8));
    }
    {
        gm_b16_8 t17 = gm_l2;
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s5v7o1bkddp5s));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(6));
        gm_b16_8 t19 = gm_f_private_name_83ij38e((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t19;
    }
    {
        gm_b16_8 t20 = gm_l3;
        gm_node *pn21 = gm_find(cx, 2126215853);
        if (!pn21) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 79, 5);
        gm_ttln0d0hd70en t22;
        t22 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn21->clauses[8])(pn21->env, pn21->hctx, pn21, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttln0d0hd70en t23 = t22;
        gm_b0_1 t24;
        if (!((((uint8_t *)&t23))[0] != 0)) {
            gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 4));
            t24 = t25;
        } else {
            gm_toe5fmr3vvf4d t26 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t23) + 4));
            gm_l4 = t26;
            gm_t6j6smj7qoctu t28;
            gm_b16_8 t29 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t28 + 0, t29);
            gm_toe5fmr3vvf4d t30 = gm_l4;
            gm_b16_8 t31 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t28 + 16, t31);
            gm_tabb710d9mq2e t32;
            gm_f_format_5uun4vl((void *)&t32, (void *)NULL, cx, NULL, &t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t33 = t32;
            if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 8, gm_src3s76fqo1a9jk, 79, 42);
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 8));
            gm_b16_8 t35 = t34;
            gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
            gm_tl572d7579hns t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
            ((uint8_t *)&t37)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t37 + 8), t36);
            gm_tl572d7579hns t38 = t37;
            uint8_t t39 = ((((uint8_t *)&t38))[0] != 0);
            F.ret = t37;
            goto gm_fret;
        }
    }
    {
        gm_b16_8 t40 = gm_l0;
        gm_b16_8 t41 = gm_l3;
        gm_f_unpack_into_kqij1d2((void *)&gm_l5, (void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t43;
        if (!((((uint8_t *)&gm_l5))[0] != 0)) goto mn45;
        t43 = ((uint8_t)0);
        goto me44;
mn45:;
        t43 = ((uint8_t)1);
        goto me44;
mn46:;
        gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 81, 8);
me44:;
        if (!t43) goto ie42;
        {
            gm_b16_8 t48 = gm_l3;
            gm_b16_8 t49 = gm_l1;
            gm_f_place_snpjvsu((void *)&(*(gm_tl572d7579hns *)((uint8_t *)&gm_l5)), (void *)NULL, cx, NULL, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix42;
ie42:;
ix42:;
    }
    {
        {
            gm_b16_8 t51 = gm_l3;
            gm_ttln0d0hd70en t52 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l5), 24);
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
__attribute__((aligned(4))) void gm_f_unpack_into_kqij1d2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_unpack_into_kqij1d2 F;
    gm_b16_8 gm_l0; /* archive */
    gm_b16_8 gm_l1; /* dir */
    gm_toe5fmr3vvf4d gm_l2; /* e */
    gm_tm98ap4112hee gm_l3; /* f */
    gm_t5liq177ejcou gm_l4; /* magic */
    uint64_t gm_l5; /* n */
    gm_b16_8 gm_l11; /* <tmp> */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* count */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    gm_b16_8 gm_l18; /* name */
    uint8_t gm_l19; /* kind */
    gm_b16_8 gm_l20; /* what */
    GM_STACK_CHECK(&F, 2709u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_t4q2b5ibsrdik t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 3), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 4), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 5), ((uint8_t)0));
        gm_node *pn4 = gm_find(cx, 2126215853);
        if (!pn4) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 87, 10);
        gm_t62ebbi2i5rkd t5;
        t5 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn4->clauses[0])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t62ebbi2i5rkd t6 = t5;
        gm_tm98ap4112hee t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            gm_tm98ap4112hee t8 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t6) + 8));
            t7 = t8;
        } else {
            gm_toe5fmr3vvf4d t9 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t6) + 8));
            gm_l2 = t9;
            gm_t6j6smj7qoctu t11;
            gm_b16_8 t12 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 0, t12);
            gm_toe5fmr3vvf4d t13 = gm_l2;
            gm_b16_8 t14 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 16, t14);
            gm_tabb710d9mq2e t15;
            gm_f_format_04pispc((void *)&t15, (void *)NULL, cx, NULL, &t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t16 = t15;
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_src3s76fqo1a9jk, 87, 65);
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            gm_b16_8 t18 = t17;
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            gm_tl572d7579hns t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
            ((uint8_t *)&t20)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t20 + 8), t19);
            gm_tl572d7579hns t21 = t20;
            uint8_t t22 = ((((uint8_t *)&t21))[0] != 0);
            F.ret = t20;
            goto gm_fret;
        }
        gm_l3 = t7;
    }
    {
    }
    {
        gm_tm98ap4112hee t23 = gm_l3;
        void * t24 = ((uint8_t *)&gm_l4);
        if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_src3s76fqo1a9jk, 89, 17);
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t24 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
        gm_node *pn26 = gm_find(cx, 2126215853);
        if (!pn26) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 89, 10);
        gm_thf5kr0is9u9i t27;
        t27 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn26->clauses[2])(pn26->env, pn26->hctx, pn26, t23, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_thf5kr0is9u9i t28 = t27;
        uint64_t t29;
        if (!((((uint8_t *)&t28))[0] != 0)) {
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&t28) + 8));
            t29 = t30;
        } else {
            t29 = ((uint64_t)0ULL);
        }
        gm_l5 = t29;
    }
    {
        gm_tm98ap4112hee t31 = gm_l3;
        gm_node *pn32 = gm_find(cx, 2126215853);
        if (!pn32) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 90, 5);
        gm_b0_1 t33;
        t33 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn32->clauses[1])(pn32->env, pn32->hctx, pn32, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t35 = 0;
        uint8_t t36 = 0;
        uint64_t t37 = gm_l5;
        uint8_t t38 = (t37 < ((uint64_t)2ULL));
        if (!t38) {
            uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
            if (t39 >= (uintptr_t)(2)) gm_trap_index(t39, 2, gm_src3s76fqo1a9jk, 91, 17);
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + t39 * 1));
            uint8_t t41 = (!(t40 == ((uint8_t)31ULL)));
            t36 = t41;
        } else t36 = 1;
        if (!t36) {
            uint64_t t42 = (uintptr_t)((uint64_t)1ULL);
            if (t42 >= (uintptr_t)(2)) gm_trap_index(t42, 2, gm_src3s76fqo1a9jk, 91, 41);
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + t42 * 1));
            uint8_t t44 = (!(t43 == ((uint8_t)139ULL)));
            t35 = t44;
        } else t35 = 1;
        if (!t35) goto ie34;
        {
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sa5v7oq7un6c6));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(83));
            gm_tl572d7579hns t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
            ((uint8_t *)&t47)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t47 + 8), t46);
            gm_tl572d7579hns t48 = t47;
            uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
            F.ret = t47;
            goto gm_fret;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s1ktljc2oac64));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l6 + 0, t50);
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sqviis18fpvj7));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&F.l6 + 16, t51);
        gm_b16_8 t52 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&F.l6 + 32, t52);
    }
    {
        void * t53 = ((uint8_t *)&F.l6);
        if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_src3s76fqo1a9jk, 96, 22);
        gm_b16_8 t54;
        GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t53 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
        gm_b16_8 t55 = t54;
        gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
        gm_f_run_mj5dobu((void *)&F.l7, (void *)NULL, cx, NULL, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
        uint8_t t59 = (uint8_t)!t58;
        if (!t59) goto ie57;
        {
            gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 24));
            gm_b16_8 t62 = gm_f_trim_fdrfe6i(t61);
            gm_tnpifv20irnph t63;
            GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t62);
            gm_tabb710d9mq2e t64;
            gm_f_format_8q6hdku((void *)&t64, (void *)NULL, cx, NULL, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t65 = t64;
            if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 8, gm_src3s76fqo1a9jk, 97, 35);
            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 8));
            gm_b16_8 t67 = t66;
            gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
            gm_tl572d7579hns t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
            ((uint8_t *)&t69)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t69 + 8), t68);
            gm_tl572d7579hns t70 = t69;
            uint8_t t71 = ((((uint8_t *)&t70))[0] != 0);
            F.ret = t69;
            goto gm_fret;
        }
        goto ix57;
ie57:;
ix57:;
    }
    {
        gm_b16_8 t72;
        GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s1ktljc2oac64));
        GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l8 + 0, t72);
        gm_b16_8 t73;
        GM_ST(void *, (uint8_t *)&t73, (void *)(gm_sfpeqbitcbq42));
        GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&F.l8 + 16, t73);
        gm_b16_8 t74 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&F.l8 + 32, t74);
    }
    {
        void * t75 = ((uint8_t *)&F.l8);
        if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_src3s76fqo1a9jk, 99, 22);
        gm_b16_8 t76;
        GM_ST(void *, (uint8_t *)&t76, (void *)(((uint8_t *)t75 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
        gm_b16_8 t77 = t76;
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
        gm_f_run_mj5dobu((void *)&F.l9, (void *)NULL, cx, NULL, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
        uint8_t t81 = (uint8_t)!t80;
        if (!t81) goto ie79;
        {
            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 24));
            gm_b16_8 t84 = gm_f_trim_fdrfe6i(t83);
            gm_tnpifv20irnph t85;
            GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t84);
            gm_tabb710d9mq2e t86;
            gm_f_format_8q6hdku((void *)&t86, (void *)NULL, cx, NULL, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t87 = t86;
            if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 8, gm_src3s76fqo1a9jk, 100, 35);
            gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&t87) + 8));
            gm_b16_8 t89 = t88;
            gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
            gm_tl572d7579hns t91;
            GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
            ((uint8_t *)&t91)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t91 + 8), t90);
            gm_tl572d7579hns t92 = t91;
            uint8_t t93 = ((((uint8_t *)&t92))[0] != 0);
            F.ret = t91;
            goto gm_fret;
        }
        goto ix79;
ie79:;
ix79:;
    }
    {
        gm_f_list_epvducg((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 8));
            gm_l11 = t95;
        }
        gm_hval t96 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn97 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlq1reae6pf9, .env = t96.env, .hctx = t96.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t100 = gm_l11;
        gm_b0_1 t101 = gm_f_lines_9uns0k2_s9kjq5m((void *)NULL, (&wn97), NULL, t100);
        if (gm_unw.kind) {
            goto gm_ll99;
        }
        F.w0 = t101;
        goto gm_ls102;
gm_ll99:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we98; }
        goto gm_lad;
gm_ls102:;
we98:;
        gm_b0_1 t103 = F.w0;
        gm_b0_1 t104 = t103;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 8));
            gm_l13 = t106;
        }
        gm_hval t107 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn108 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlhtabotcjhg, .env = t107.env, .hctx = t107.cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
        gm_b16_8 t111 = gm_l13;
        gm_b0_1 t112 = gm_f_lines_9uns0k2_s84ppfm((void *)NULL, (&wn108), NULL, t111);
        if (gm_unw.kind) {
            goto gm_ll110;
        }
        F.w1 = t112;
        goto gm_ls113;
gm_ll110:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we109; }
        goto gm_lad;
gm_ls113:;
we109:;
        gm_b0_1 t114 = F.w1;
        gm_b0_1 t115 = t114;
    }
    {
        gm_l14 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            gm_b16_8 t118 = t117;
            gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
            gm_l15 = t119;
        }
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t121 = gm_l16;
                uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t123 = (t121 < t122);
                if (!t123) goto lb120;
                {
                    gm_tf1bjg81u0glv t126;
                    uint64_t t127 = gm_l16;
                    GM_ST(uint64_t, (uint8_t *)&t126 + 0, t127);
                    uint64_t t128 = gm_l16;
                    uint64_t t129 = (uintptr_t)t128;
                    uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t129 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t126 + 8, t131);
                    gm_tf1bjg81u0glv t132 = t126;
                    uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&t132) + 0));
                    gm_l17 = t135;
                    gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&t132) + 8));
                    gm_l18 = t136;
                    {
                        uint64_t t138 = gm_l14;
                        uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                        if (o140) gm_trap("integer overflow in `+`", gm_src3s76fqo1a9jk, 107, 9);
                        uint64_t t141 = r139;
                        gm_l14 = t141;
                    }
                    {
                        uint64_t t143 = gm_l14;
                        uint8_t t144 = (t143 > ((uint64_t)1048576ULL));
                        if (!t144) goto ie142;
                        {
                            gm_t19enj90vg8bd t146;
                            GM_ST(int32_t, ((uint8_t *)&t146 + 0), ((int32_t)1048576LL));
                            gm_tabb710d9mq2e t147;
                            gm_f_format_ep7kj4f((void *)&t147, (void *)NULL, cx, NULL, t146);
                            if (gm_unw.kind) {
                                goto gm_ll124;
                            }
                            gm_tabb710d9mq2e t148 = t147;
                            if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 8, gm_src3s76fqo1a9jk, 109, 21);
                            gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&t148) + 8));
                            gm_b16_8 t150 = t149;
                            gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                            gm_tl572d7579hns t152;
                            GM_ST(uint8_t, (uint8_t *)&t152 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t152 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t152 + 4, 0);
                            ((uint8_t *)&t152)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t152 + 8), t151);
                            gm_tl572d7579hns t153 = t152;
                            uint8_t t154 = ((((uint8_t *)&t153))[0] != 0);
                            F.ret = t152;
                            goto gm_fret;
                        }
                        goto ix142;
ie142:;
ix142:;
                    }
                    {
                        gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                        gm_b16_8 t156 = t155;
                        void * t157 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t156)));
                        uint64_t t158 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t156) + 8);
                        uint64_t t159 = gm_l17;
                        uint64_t t160 = (uintptr_t)t159;
                        gm_tqs5p4n2tq19v t161;
                        if (t160 < t158) {
                        uint8_t t162 = GM_LD(uint8_t, ((uint8_t *)t157 + t160 * 1));
                        gm_tqs5p4n2tq19v t163;
                        GM_ST(uint8_t, ((uint8_t *)&t163), t162);
                        ((uint8_t *)&t163)[1] = 1;
                            t161 = t163;
                        } else {
                        gm_tqs5p4n2tq19v t164;
                        memset(&t164, 0, sizeof t164);
                            t161 = t164;
                        }
                        gm_tqs5p4n2tq19v t165 = t161;
                        uint8_t t166;
                        if (((((uint8_t *)&t165))[1] != 0)) {
                            uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&t165) + 0));
                            t166 = t167;
                        } else {
                            t166 = ((uint8_t)45ULL);
                        }
                        gm_l19 = t166;
                    }
                    {
                        uint8_t t169 = gm_l19;
                        gm_toh0ootodhjue t170;
                        GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)45ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t170 + 1), ((uint8_t)100ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t170 + 2), ((uint8_t)108ULL));
                        gm_toh0ootodhjue t171 = t170;
                        uint8_t t172 = 0;
                        uint8_t t173 = t169;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t171) + 0, ((uint8_t *)&t173))) t172 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t171) + 1, ((uint8_t *)&t173))) t172 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t171) + 2, ((uint8_t *)&t173))) t172 = 1;
                        uint8_t t174 = (uint8_t)(!t172);
                        if (!t174) goto ie168;
                        {
                            gm_b16_8 t176;
                            uint8_t t178 = gm_l19;
                            uint8_t t179 = ((t178 == ((uint8_t)104ULL)));
                            if (!t179) goto ie177;
                            gm_b16_8 t181;
                            GM_ST(void *, (uint8_t *)&t181, (void *)(gm_srrq1irp1jfep));
                            GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(11));
                            gm_b16_8 t182 = t181;
                            t176 = t182;
                            goto ix177;
ie177:;
                            gm_b16_8 t184;
                            GM_ST(void *, (uint8_t *)&t184, (void *)(gm_s8nekj984lthm));
                            GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(14));
                            gm_b16_8 t185 = t184;
                            t176 = t185;
ix177:;
                            gm_l20 = t176;
                        }
                        {
                            gm_t6j6smj7qoctu t186;
                            gm_b16_8 t187 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t186 + 0, t187);
                            gm_b16_8 t188 = gm_l18;
                            GM_ST(gm_b16_8, (uint8_t *)&t186 + 16, t188);
                            gm_tabb710d9mq2e t189;
                            gm_f_format_5pl0mkc((void *)&t189, (void *)NULL, cx, NULL, &t186);
                            if (gm_unw.kind) {
                                goto gm_ll124;
                            }
                            gm_tabb710d9mq2e t190 = t189;
                            if (((((uint8_t *)&t190))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t190) + 8, gm_src3s76fqo1a9jk, 114, 21);
                            gm_b16_8 t191 = GM_LD(gm_b16_8, (((uint8_t *)&t190) + 8));
                            gm_b16_8 t192 = t191;
                            gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
                            gm_tl572d7579hns t194;
                            GM_ST(uint8_t, (uint8_t *)&t194 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t194 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t194 + 4, 0);
                            ((uint8_t *)&t194)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t194 + 8), t193);
                            gm_tl572d7579hns t195 = t194;
                            uint8_t t196 = ((((uint8_t *)&t195))[0] != 0);
                            F.ret = t194;
                            goto gm_fret;
                        }
                        goto ix168;
ie168:;
ix168:;
                    }
                    {
                        gm_b16_8 t198 = gm_l18;
                        uint8_t t199 = gm_f_leaves_directory_kq5llq4((void *)NULL, cx, NULL, t198);
                        if (gm_unw.kind) {
                            goto gm_ll124;
                        }
                        if (!t199) goto ie197;
                        {
                            gm_b16_8 t201 = gm_l18;
                            gm_tnpifv20irnph t202;
                            GM_ST(gm_b16_8, ((uint8_t *)&t202 + 0), t201);
                            gm_tabb710d9mq2e t203;
                            gm_f_format_nk3ghj0((void *)&t203, (void *)NULL, cx, NULL, t202);
                            if (gm_unw.kind) {
                                goto gm_ll124;
                            }
                            gm_tabb710d9mq2e t204 = t203;
                            if (((((uint8_t *)&t204))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t204) + 8, gm_src3s76fqo1a9jk, 117, 21);
                            gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&t204) + 8));
                            gm_b16_8 t206 = t205;
                            gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&t206));
                            gm_tl572d7579hns t208;
                            GM_ST(uint8_t, (uint8_t *)&t208 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t208 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t208 + 4, 0);
                            ((uint8_t *)&t208)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t208 + 8), t207);
                            gm_tl572d7579hns t209 = t208;
                            uint8_t t210 = ((((uint8_t *)&t209))[0] != 0);
                            F.ret = t208;
                            goto gm_fret;
                        }
                        goto ix197;
ie197:;
ix197:;
                    }
                    goto me133;
mn134:;
                    gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 106, 5);
me133:;
                }
lc120:;
                {
                    uint64_t t212 = gm_l16;
                    uint64_t r213; bool o214 = __builtin_add_overflow(t212, ((uint64_t)1ULL), &r213);
                    uint64_t t215 = r213;
                    gm_l16 = t215;
                }
            }
gm_ll124:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb120; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc120; }
            }
            goto gm_lad;
lb120:;
        }
    }
    {
        gm_b16_8 t216;
        GM_ST(void *, (uint8_t *)&t216, (void *)(gm_s1ktljc2oac64));
        GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l21 + 0, t216);
        gm_b16_8 t217;
        GM_ST(void *, (uint8_t *)&t217, (void *)(gm_sos62dipmhb8k));
        GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&F.l21 + 16, t217);
        gm_b16_8 t218 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&F.l21 + 32, t218);
        gm_b16_8 t219;
        GM_ST(void *, (uint8_t *)&t219, (void *)(gm_sppohbjp90mol));
        GM_ST(uintptr_t, (uint8_t *)&t219 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l21 + 48, t219);
        gm_b16_8 t220 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&F.l21 + 64, t220);
        gm_b16_8 t221;
        GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sobnp9i68hvdj));
        GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(15));
        GM_ST(gm_b16_8, (uint8_t *)&F.l21 + 80, t221);
        gm_b16_8 t222;
        GM_ST(void *, (uint8_t *)&t222, (void *)(gm_snhr2rebnnkc2));
        GM_ST(uintptr_t, (uint8_t *)&t222 + 8, (uintptr_t)(21));
        GM_ST(gm_b16_8, (uint8_t *)&F.l21 + 96, t222);
    }
    {
        void * t223 = ((uint8_t *)&F.l21);
        if ((uintptr_t)0 > (uintptr_t)7 || (uintptr_t)7 > (uintptr_t)7) gm_trap_slice((uintptr_t)0, (uintptr_t)7, 7, gm_src3s76fqo1a9jk, 122, 14);
        gm_b16_8 t224;
        GM_ST(void *, (uint8_t *)&t224, (void *)(((uint8_t *)t223 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)((uintptr_t)7 - (uintptr_t)0));
        gm_b16_8 t225 = t224;
        gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
        gm_f_run_mj5dobu((void *)&F.l22, (void *)NULL, cx, NULL, t226);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
        uint8_t t229 = (uint8_t)!t228;
        if (!t229) goto ie227;
        {
            gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 24));
            gm_b16_8 t232 = gm_f_trim_fdrfe6i(t231);
            gm_tnpifv20irnph t233;
            GM_ST(gm_b16_8, ((uint8_t *)&t233 + 0), t232);
            gm_tabb710d9mq2e t234;
            gm_f_format_0rhberc((void *)&t234, (void *)NULL, cx, NULL, t233);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t235 = t234;
            if (((((uint8_t *)&t235))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t235) + 8, gm_src3s76fqo1a9jk, 123, 27);
            gm_b16_8 t236 = GM_LD(gm_b16_8, (((uint8_t *)&t235) + 8));
            gm_b16_8 t237 = t236;
            gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&t237));
            gm_tl572d7579hns t239;
            GM_ST(uint8_t, (uint8_t *)&t239 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t239 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t239 + 4, 0);
            ((uint8_t *)&t239)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t239 + 8), t238);
            gm_tl572d7579hns t240 = t239;
            uint8_t t241 = ((((uint8_t *)&t240))[0] != 0);
            F.ret = t239;
            goto gm_fret;
        }
        goto ix227;
ie227:;
ix227:;
    }
    {
        gm_b16_8 t242 = gm_l1;
        gm_b0_1 t243 = gm_f_sanitize_modes_mvteloh((void *)NULL, cx, NULL, t242);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b0_1 t244;
        gm_tl572d7579hns t245;
        GM_ST(uint8_t, (uint8_t *)&t245 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t245 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t245 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t245 + 16, 0);
        ((uint8_t *)&t245)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t245 + 8), t244);
        gm_tl572d7579hns t246 = t245;
        uint8_t t247 = ((((uint8_t *)&t246))[0] != 0);
        F.ret = t245;
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
