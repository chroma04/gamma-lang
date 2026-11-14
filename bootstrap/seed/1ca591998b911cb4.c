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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint8_t v1; } f1; } gm_t1ec8br9ddkav;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1ec8br9ddkav) == 32 && _Alignof(gm_t1ec8br9ddkav) == 8 && offsetof(gm_t1ec8br9ddkav, f0) == 0 && offsetof(gm_t1ec8br9ddkav, f1) == 8, "layout of `Flow!bool`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_2p7d2kv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_fufbgf7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_truncate_j7f0pmi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_vector_builtin_3fn933h {
    gm_frame h;
    gm_trqfvmjtt4sij l23; /* args */
    gm_trqfvmjtt4sij l51; /* args */
    gm_tcfa6l7reoosn ret;
};
__attribute__((aligned(4))) void gm_f_apply_builtin_41dffti(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
__attribute__((aligned(4))) void gm_f_binop_nrsq3mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
__attribute__((aligned(4))) void gm_f_cmp_op_mflafbf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_convert_is6an2k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_deinit_tq06jtm(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void);
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_truncate_j7f0pmi(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) void gm_f_vector_builtin_3fn933h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
extern const uint8_t gm_s1tv0n9hk7497[58];
extern const uint8_t gm_s4lpfn3di4rp9[66];
extern const uint8_t gm_s5e5797b1jurs[20];
extern const uint8_t gm_scqqkvgm1j3cs[65];
extern const uint8_t gm_sm94b3smoinpd[60];
extern const uint8_t gm_smsrao7h22muv[61];
extern const uint8_t gm_spnr4ftjbm9m9[61];
extern const uint8_t gm_spqlqjii789ln[57];
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcns60k96jj1mt[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ssvci9220kuoj[45];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t2 == t3));
        if (!t4) {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            gm_b16_8 t6 = t5;
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t6) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t6)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = gm_l1;
        gm_b16_8 t10 = t9;
        if (GM_LD(uintptr_t, ((uint8_t *)&t8) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t8)), GM_LD(void *, ((uint8_t *)&t10)), GM_LD(uintptr_t, ((uint8_t *)&t8) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2p7d2kv F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&gm_l0));
        gm_l2 = t2;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
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
                        uint64_t t17 = gm_l5;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = gm_l2;
                        uint8_t t20 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t19 + t18 * 1), t20);
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t22 = gm_l4;
                    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                    uint64_t t25 = r23;
                    gm_l4 = t25;
                }
            }
lb5:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
    uint8_t t4 = (t3 <= ((uint64_t)8ULL));
    if (!t4) goto ie2;
    void * t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    uint64_t t8 = gm_f_rd_word_8flcq6o(t6, t7);
    uint64_t t9 = t8;
    t1 = t9;
    goto ix2;
ie2:;
    void * t11 = gm_l0;
    gm_b16_8 t12 = gm_l1;
    unsigned __int128 t13 = gm_f_get_uint_bq8c4b7(t11, t12);
    uint64_t t14 = (uint64_t)t13;
    uint64_t t15 = t14;
    t1 = t15;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_htv6093 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 8));
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
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fufbgf7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_2p7d2kv();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_kpppm1j F;
    void * gm_l0; /* m */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)t1) + 360) + 40));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 360) + 48));
    gm_t2lqu8jsj511d t5;
    GM_ST(uint64_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 89));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)8ULL)));
            if (!t8) goto ie6;
            {
                uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 316, 36);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1));
                uint64_t t13 = (uint64_t)t12;
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 316, 49);
                uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1));
                uint64_t t17 = (uint64_t)t16;
                unsigned __int128 n18 = (unsigned __int128)((uint64_t)8ULL);
                if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t r19 = (uint64_t)((uint64_t)t17 << (int)n18);
                bool o20 = (uint64_t)(r19 >> (int)n18) != t17;
                if (o20) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t t21 = r19;
                uint64_t t22 = (uint64_t)(t13 | t21);
                uint64_t t23 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcnqtpfma6qe4a, 316, 68);
                uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t23 * 1));
                uint64_t t26 = (uint64_t)t25;
                unsigned __int128 n27 = (unsigned __int128)((uint64_t)16ULL);
                if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t r28 = (uint64_t)((uint64_t)t26 << (int)n27);
                bool o29 = (uint64_t)(r28 >> (int)n27) != t26;
                if (o29) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t22 | t30);
                uint64_t t32 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 316, 88);
                uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1));
                uint64_t t35 = (uint64_t)t34;
                unsigned __int128 n36 = (unsigned __int128)((uint64_t)24ULL);
                if (n36 >= 64) gm_trap_shift((uint64_t)n36, "u64", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t r37 = (uint64_t)((uint64_t)t35 << (int)n36);
                bool o38 = (uint64_t)(r37 >> (int)n36) != t35;
                if (o38) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t t39 = r37;
                uint64_t t40 = (uint64_t)(t31 | t39);
                uint64_t t41 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcnqtpfma6qe4a, 316, 108);
                uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t41 * 1));
                uint64_t t44 = (uint64_t)t43;
                unsigned __int128 n45 = (unsigned __int128)((uint64_t)32ULL);
                if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t r46 = (uint64_t)((uint64_t)t44 << (int)n45);
                bool o47 = (uint64_t)(r46 >> (int)n45) != t44;
                if (o47) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t t48 = r46;
                uint64_t t49 = (uint64_t)(t40 | t48);
                uint64_t t50 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 316, 128);
                uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1));
                uint64_t t53 = (uint64_t)t52;
                unsigned __int128 n54 = (unsigned __int128)((uint64_t)40ULL);
                if (n54 >= 64) gm_trap_shift((uint64_t)n54, "u64", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t r55 = (uint64_t)((uint64_t)t53 << (int)n54);
                bool o56 = (uint64_t)(r55 >> (int)n54) != t53;
                if (o56) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t t57 = r55;
                uint64_t t58 = (uint64_t)(t49 | t57);
                uint64_t t59 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srcnqtpfma6qe4a, 316, 148);
                uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t59 * 1));
                uint64_t t62 = (uint64_t)t61;
                unsigned __int128 n63 = (unsigned __int128)((uint64_t)48ULL);
                if (n63 >= 64) gm_trap_shift((uint64_t)n63, "u64", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t r64 = (uint64_t)((uint64_t)t62 << (int)n63);
                bool o65 = (uint64_t)(r64 >> (int)n63) != t62;
                if (o65) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t t66 = r64;
                uint64_t t67 = (uint64_t)(t58 | t66);
                uint64_t t68 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_srcnqtpfma6qe4a, 316, 168);
                uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t68 * 1));
                uint64_t t71 = (uint64_t)t70;
                unsigned __int128 n72 = (unsigned __int128)((uint64_t)56ULL);
                if (n72 >= 64) gm_trap_shift((uint64_t)n72, "u64", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t r73 = (uint64_t)((uint64_t)t71 << (int)n72);
                bool o74 = (uint64_t)(r73 >> (int)n72) != t71;
                if (o74) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t t75 = r73;
                uint64_t t76 = (uint64_t)(t67 | t75);
                uint8_t t77 = 0;
                F.ret = t76;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t80 = ((t79 == ((uint64_t)4ULL)));
            if (!t80) goto ie78;
            {
                uint64_t t82 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 317, 36);
                uint8_t t84 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1));
                uint64_t t85 = (uint64_t)t84;
                uint64_t t86 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcnqtpfma6qe4a, 317, 49);
                uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t86 * 1));
                uint64_t t89 = (uint64_t)t88;
                unsigned __int128 n90 = (unsigned __int128)((uint64_t)8ULL);
                if (n90 >= 64) gm_trap_shift((uint64_t)n90, "u64", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t r91 = (uint64_t)((uint64_t)t89 << (int)n90);
                bool o92 = (uint64_t)(r91 >> (int)n90) != t89;
                if (o92) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t t93 = r91;
                uint64_t t94 = (uint64_t)(t85 | t93);
                uint64_t t95 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t95 >= (uintptr_t)(t96)) gm_trap_index(t95, t96, gm_srcnqtpfma6qe4a, 317, 68);
                uint8_t t97 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t95 * 1));
                uint64_t t98 = (uint64_t)t97;
                unsigned __int128 n99 = (unsigned __int128)((uint64_t)16ULL);
                if (n99 >= 64) gm_trap_shift((uint64_t)n99, "u64", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t r100 = (uint64_t)((uint64_t)t98 << (int)n99);
                bool o101 = (uint64_t)(r100 >> (int)n99) != t98;
                if (o101) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t t102 = r100;
                uint64_t t103 = (uint64_t)(t94 | t102);
                uint64_t t104 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcnqtpfma6qe4a, 317, 88);
                uint8_t t106 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 1));
                uint64_t t107 = (uint64_t)t106;
                unsigned __int128 n108 = (unsigned __int128)((uint64_t)24ULL);
                if (n108 >= 64) gm_trap_shift((uint64_t)n108, "u64", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t r109 = (uint64_t)((uint64_t)t107 << (int)n108);
                bool o110 = (uint64_t)(r109 >> (int)n108) != t107;
                if (o110) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t t111 = r109;
                uint64_t t112 = (uint64_t)(t103 | t111);
                uint8_t t113 = 0;
                F.ret = t112;
                goto gm_fret;
            }
            goto ix78;
ie78:;
ix78:;
        }
        {
            uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t116 = ((t115 == ((uint64_t)1ULL)));
            if (!t116) goto ie114;
            {
                uint64_t t118 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srcnqtpfma6qe4a, 318, 36);
                uint8_t t120 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t118 * 1));
                uint64_t t121 = (uint64_t)t120;
                uint8_t t122 = 0;
                F.ret = t121;
                goto gm_fret;
            }
            goto ix114;
ie114:;
ix114:;
        }
        {
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t125 = ((t124 == ((uint64_t)2ULL)));
            if (!t125) goto ie123;
            {
                uint64_t t127 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcnqtpfma6qe4a, 319, 36);
                uint8_t t129 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t127 * 1));
                uint64_t t130 = (uint64_t)t129;
                uint64_t t131 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t131 >= (uintptr_t)(t132)) gm_trap_index(t131, t132, gm_srcnqtpfma6qe4a, 319, 49);
                uint8_t t133 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t131 * 1));
                uint64_t t134 = (uint64_t)t133;
                unsigned __int128 n135 = (unsigned __int128)((uint64_t)8ULL);
                if (n135 >= 64) gm_trap_shift((uint64_t)n135, "u64", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t r136 = (uint64_t)((uint64_t)t134 << (int)n135);
                bool o137 = (uint64_t)(r136 >> (int)n135) != t134;
                if (o137) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t t138 = r136;
                uint64_t t139 = (uint64_t)(t130 | t138);
                uint8_t t140 = 0;
                F.ret = t139;
                goto gm_fret;
            }
            goto ix123;
ie123:;
ix123:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t141 = gm_l0;
        gm_b16_8 t142 = gm_l1;
        unsigned __int128 t143 = gm_f_get_uint_bq8c4b7(t141, t142);
        uint64_t t144 = (uint64_t)t143;
        uint8_t t145 = 0;
        F.ret = t144;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_release_jn5kqdi F;
    void * gm_l0; /* m */
    gm_t2lqu8jsj511d gm_l1; /* mk */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
        GM_ST(uint64_t, ((((uint8_t *)t2) + 360) + 40), t3);
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        GM_ST(uint64_t, ((((uint8_t *)t4) + 360) + 48), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 360);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 40));
        uint64_t t6 = (uintptr_t)t5;
        void * t7 = gm_l2;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 0) + 0) + 8);
        if (t6 >= (uintptr_t)(t8)) gm_trap_index(t6, t8, gm_srcpit8mdn0781q, 386, 14);
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 0) + 0)) + t6 * 16));
        gm_l3 = t9;
    }
    {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t11 = (uintptr_t)t10;
        gm_l4 = t11;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
        uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t17 = r15;
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t20 = r18;
        uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
        uint64_t t22 = (uint64_t)~t21;
        uint64_t t23 = (uint64_t)(t20 & t22);
        uint64_t t24 = gm_l4;
        uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
        if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 390, 12);
        uint64_t t27 = r25;
        gm_l5 = t27;
    }
    {
        uint64_t t28 = gm_l1;
        uint64_t t29 = gm_f_us_cuv01fp(t28);
        gm_l6 = t29;
    }
    {
        uint8_t t31 = 0;
        uint64_t t32 = gm_l1;
        uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
        if (!t33) {
            uint64_t t34 = gm_l5;
            uint64_t t35 = gm_l6;
            uint64_t r36; bool o37 = __builtin_add_overflow(t34, t35, &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 392, 18);
            uint64_t t38 = r36;
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t40 = (t38 > t39);
            t31 = t40;
        } else t31 = 1;
        if (!t31) goto ie30;
        {
            void * t42 = gm_l0;
            uint64_t t43 = gm_l1;
            gm_b16_8 t44 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t47 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t48 = gm_l5;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = gm_l5;
        uint64_t t51 = gm_l6;
        uint64_t r52; bool o53 = __builtin_add_overflow(t50, t51, &r52);
        if (o53) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 21);
        uint64_t t54 = r52;
        uint64_t t55 = (uintptr_t)t54;
        if (t49 > t55 || t55 > (uintptr_t)t46) gm_trap_slice(t49, t55, t46, gm_srcpit8mdn0781q, 393, 10);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t47 + t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t49));
        gm_l7 = t56;
    }
    {
        void * t57 = gm_l2;
        uint64_t t58 = gm_l5;
        uint64_t t59 = gm_l6;
        uint64_t r60; bool o61 = __builtin_add_overflow(t58, t59, &r60);
        if (o61) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 14);
        uint64_t t62 = r60;
        GM_ST(uint64_t, (((uint8_t *)t57) + 48), t62);
    }
    {
        gm_b16_8 t63 = gm_l7;
        gm_f_fill_abmf5g5(t63, ((uint8_t)0ULL));
    }
    {
        gm_b16_8 t64 = gm_l7;
        uint8_t t65 = 0;
        F.ret = t64;
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
inline __attribute__((aligned(4))) void gm_f_truncate_j7f0pmi(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_j7f0pmi F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 16)));
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
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_70vvk1e F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_l2;
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    gm_tabb710d9mq2e t5;
    gm_f_format_46el73v((void *)&t5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t6 = t5;
    if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcpit8mdn0781q, 437, 107);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t9);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = gm_l0;
    uint64_t t2 = (uint64_t)t1;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_vector_builtin_3fn933h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7) {
    struct gm_fr_vector_builtin_3fn933h F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* res_ty */
    gm_t88rcucg1b7uk gm_l4; /* t0 */
    gm_t88rcucg1b7uk gm_l5; /* t1 */
    gm_b16_8 gm_l6; /* v */
    gm_b16_8 gm_l7; /* dst */
    uint64_t gm_l8; /* n */
    gm_t88rcucg1b7uk gm_l9; /* lane */
    uint64_t gm_l10; /* sz */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    gm_b16_8 gm_l14; /* x */
    gm_b16_8 gm_l15; /* y */
    uint8_t gm_l16; /* r */
    uint64_t gm_l17; /* n */
    gm_t88rcucg1b7uk gm_l18; /* lane */
    uint64_t gm_l19; /* sz */
    gm_t2lqu8jsj511d gm_l20; /* mk */
    gm_b16_8 gm_l21; /* acc */
    gm_b16_8 gm_l22; /* next */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* i */
    gm_b16_8 gm_l27; /* x */
    gm_tvdg94bk7aa22 gm_l28; /* scalar */
    uint8_t gm_l29; /* r */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint8_t gm_l32; /* x */
    uint8_t gm_l33; /* r */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint8_t gm_l36; /* x */
    uint64_t gm_l37; /* c */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint8_t gm_l40; /* x */
    uint64_t gm_l41; /* n */
    gm_t88rcucg1b7uk gm_l42; /* lane */
    uint64_t gm_l43; /* sz */
    uint64_t gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    uint64_t gm_l46; /* i */
    uint64_t gm_l47; /* n */
    gm_t88rcucg1b7uk gm_l48; /* lane */
    uint64_t gm_l49; /* sz */
    gm_tvdg94bk7aa22 gm_l50; /* scalar */
    uint64_t gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    uint64_t gm_l54; /* i */
    gm_b16_8 gm_l55; /* <tmp> */
    uint64_t gm_l56; /* <tmp> */
    gm_b16_8 gm_l57; /* x */
    uint64_t gm_l58; /* n */
    gm_t88rcucg1b7uk gm_l59; /* from */
    gm_t88rcucg1b7uk gm_l60; /* to */
    uint64_t gm_l61; /* fsz */
    uint64_t gm_l62; /* tsz */
    gm_tvdg94bk7aa22 gm_l63; /* how */
    uint64_t gm_l64; /* <tmp> */
    uint64_t gm_l65; /* <tmp> */
    uint64_t gm_l66; /* i */
    uint64_t gm_l67; /* n */
    gm_t88rcucg1b7uk gm_l68; /* lane */
    uint64_t gm_l69; /* sz */
    uint64_t gm_l70; /* <tmp> */
    uint64_t gm_l71; /* <tmp> */
    uint64_t gm_l72; /* j */
    uint64_t gm_l73; /* k */
    GM_STACK_CHECK(&F, 8133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    {
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma13; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma13; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma13; case ((uint8_t)27ULL): goto ma13; case ((uint8_t)28ULL): goto ma13; case ((uint8_t)29ULL): goto ma13; case ((uint8_t)30ULL): goto ma13; case ((uint8_t)31ULL): goto ma13; case ((uint8_t)32ULL): goto ma13; case ((uint8_t)33ULL): goto ma13; case ((uint8_t)34ULL): goto ma13; case ((uint8_t)35ULL): goto ma13; case ((uint8_t)36ULL): goto ma13; case ((uint8_t)37ULL): goto ma13; case ((uint8_t)38ULL): goto ma13; case ((uint8_t)39ULL): goto ma13; case ((uint8_t)40ULL): goto ma4; case ((uint8_t)41ULL): goto ma4; case ((uint8_t)42ULL): goto ma4; case ((uint8_t)43ULL): goto ma4; case ((uint8_t)44ULL): goto ma4; case ((uint8_t)45ULL): goto ma4; case ((uint8_t)46ULL): goto ma5; case ((uint8_t)47ULL): goto ma5; case ((uint8_t)48ULL): goto ma5; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma7; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma9; case ((uint8_t)53ULL): goto ma10; case ((uint8_t)54ULL): goto ma10; case ((uint8_t)55ULL): goto ma10; case ((uint8_t)56ULL): goto ma11; case ((uint8_t)57ULL): goto ma12; case ((uint8_t)58ULL): goto ma13; case ((uint8_t)59ULL): goto ma13; case ((uint8_t)60ULL): goto ma13; case ((uint8_t)61ULL): goto ma13; case ((uint8_t)62ULL): goto ma13; case ((uint8_t)63ULL): goto ma13; case ((uint8_t)64ULL): goto ma13; case ((uint8_t)65ULL): goto ma13; case ((uint8_t)66ULL): goto ma13; case ((uint8_t)67ULL): goto ma13; case ((uint8_t)68ULL): goto ma13; case ((uint8_t)69ULL): goto ma13; case ((uint8_t)70ULL): goto ma13; case ((uint8_t)71ULL): goto ma13; case ((uint8_t)72ULL): goto ma13; case ((uint8_t)73ULL): goto ma13; case ((uint8_t)74ULL): goto ma13; case ((uint8_t)75ULL): goto ma13; case ((uint8_t)76ULL): goto ma13; case ((uint8_t)77ULL): goto ma13; case ((uint8_t)78ULL): goto ma13; case ((uint8_t)79ULL): goto ma13; case ((uint8_t)80ULL): goto ma13; case ((uint8_t)81ULL): goto ma13; default: goto ma13; }
ma4:;
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t19 != ((uint8_t)40ULL)) goto pa18;
            goto po17;
pa18:;
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t21 != ((uint8_t)41ULL)) goto pa20;
            goto po17;
pa20:;
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t23 != ((uint8_t)42ULL)) goto pa22;
            goto po17;
pa22:;
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t25 != ((uint8_t)43ULL)) goto pa24;
            goto po17;
pa24:;
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t27 != ((uint8_t)44ULL)) goto pa26;
            goto po17;
pa26:;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t29 != ((uint8_t)45ULL)) goto pa28;
            goto po17;
pa28:;
            goto mn16;
po17:;
            {
                void * t32 = gm_l0;
                gm_t88rcucg1b7uk t33 = gm_l4;
                gm_t53at80pfdus3 t34;
                gm_f_kind_htv6093((void *)&t34, t32, GM_CONV(gm_csguankko4lmhd, t33));
                gm_t53at80pfdus3 t35 = t34;
                uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
                if (t36 != ((uint8_t)11ULL)) goto ie31;
                uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)&t35) + 8) + 0));
                gm_l8 = t37;
                gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t35) + 8) + 8));
                gm_l9 = t38;
                {
                    void * t40 = gm_l0;
                    gm_t88rcucg1b7uk t41 = gm_l9;
                    uint64_t t42 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t40, t41);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t43 = gm_f_us_cuv01fp(t42);
                    gm_l10 = t43;
                }
                {
                    {
                        gm_l12 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t45 = gm_l8;
                        uint64_t t46 = gm_f_us_cuv01fp(t45);
                        gm_l11 = t46;
                    }
                    {
                        for (;;) {
                            uint64_t t48 = gm_l12;
                            uint64_t t49 = gm_l11;
                            uint8_t t50 = (t48 < t49);
                            if (!t50) goto lb47;
                            {
                                uint64_t t53 = gm_l12;
                                uint64_t t54 = t53;
                                uint64_t t57 = GM_LD(uint64_t, ((uint8_t *)&t54));
                                gm_l13 = t57;
                                {
                                    uint64_t t59 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srcns60k96jj1mt, 12, 22);
                                    uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t59 * 16) + 8);
                                    void * t62 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t59 * 16)));
                                    uint64_t t63 = gm_l13;
                                    uint64_t t64 = gm_l10;
                                    uint64_t r65; bool o66 = __builtin_mul_overflow(t63, t64, &r65);
                                    if (o66) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 12, 27);
                                    uint64_t t67 = r65;
                                    uint64_t t68 = (uintptr_t)t67;
                                    uint64_t t69 = gm_l13;
                                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                                    if (o71) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 12, 36);
                                    uint64_t t72 = r70;
                                    uint64_t t73 = gm_l10;
                                    uint64_t r74; bool o75 = __builtin_mul_overflow(t72, t73, &r74);
                                    if (o75) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 12, 35);
                                    uint64_t t76 = r74;
                                    uint64_t t77 = (uintptr_t)t76;
                                    if (t68 > t77 || t77 > (uintptr_t)t61) gm_trap_slice(t68, t77, t61, gm_srcns60k96jj1mt, 12, 22);
                                    gm_b16_8 t78;
                                    GM_ST(void *, (uint8_t *)&t78, (void *)(((uint8_t *)t62 + t68 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(t77 - t68));
                                    gm_l14 = t78;
                                }
                                {
                                    uint64_t t79 = (uintptr_t)((uint64_t)1ULL);
                                    uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    if (t79 >= (uintptr_t)(t80)) gm_trap_index(t79, t80, gm_srcns60k96jj1mt, 13, 22);
                                    uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t79 * 16) + 8);
                                    void * t82 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t79 * 16)));
                                    uint64_t t83 = gm_l13;
                                    uint64_t t84 = gm_l10;
                                    uint64_t r85; bool o86 = __builtin_mul_overflow(t83, t84, &r85);
                                    if (o86) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 13, 27);
                                    uint64_t t87 = r85;
                                    uint64_t t88 = (uintptr_t)t87;
                                    uint64_t t89 = gm_l13;
                                    uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                                    if (o91) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 13, 36);
                                    uint64_t t92 = r90;
                                    uint64_t t93 = gm_l10;
                                    uint64_t r94; bool o95 = __builtin_mul_overflow(t92, t93, &r94);
                                    if (o95) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 13, 35);
                                    uint64_t t96 = r94;
                                    uint64_t t97 = (uintptr_t)t96;
                                    if (t88 > t97 || t97 > (uintptr_t)t81) gm_trap_slice(t88, t97, t81, gm_srcns60k96jj1mt, 13, 22);
                                    gm_b16_8 t98;
                                    GM_ST(void *, (uint8_t *)&t98, (void *)(((uint8_t *)t82 + t88 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(t97 - t88));
                                    gm_l15 = t98;
                                }
                                {
                                    uint8_t t99;
                                    uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    switch (t108) { case ((uint8_t)0ULL): goto ma106; case ((uint8_t)1ULL): goto ma106; case ((uint8_t)2ULL): goto ma106; case ((uint8_t)3ULL): goto ma106; case ((uint8_t)4ULL): goto ma106; case ((uint8_t)5ULL): goto ma106; case ((uint8_t)6ULL): goto ma106; case ((uint8_t)7ULL): goto ma106; case ((uint8_t)8ULL): goto ma106; case ((uint8_t)9ULL): goto ma106; case ((uint8_t)10ULL): goto ma106; case ((uint8_t)11ULL): goto ma106; case ((uint8_t)12ULL): goto ma106; case ((uint8_t)13ULL): goto ma106; case ((uint8_t)14ULL): goto ma106; case ((uint8_t)15ULL): goto ma106; case ((uint8_t)16ULL): goto ma106; case ((uint8_t)17ULL): goto ma106; case ((uint8_t)18ULL): goto ma106; case ((uint8_t)19ULL): goto ma106; case ((uint8_t)20ULL): goto ma106; case ((uint8_t)21ULL): goto ma106; case ((uint8_t)22ULL): goto ma106; case ((uint8_t)23ULL): goto ma106; case ((uint8_t)24ULL): goto ma106; case ((uint8_t)25ULL): goto ma106; case ((uint8_t)26ULL): goto ma106; case ((uint8_t)27ULL): goto ma106; case ((uint8_t)28ULL): goto ma106; case ((uint8_t)29ULL): goto ma106; case ((uint8_t)30ULL): goto ma106; case ((uint8_t)31ULL): goto ma106; case ((uint8_t)32ULL): goto ma106; case ((uint8_t)33ULL): goto ma106; case ((uint8_t)34ULL): goto ma106; case ((uint8_t)35ULL): goto ma106; case ((uint8_t)36ULL): goto ma106; case ((uint8_t)37ULL): goto ma106; case ((uint8_t)38ULL): goto ma106; case ((uint8_t)39ULL): goto ma106; case ((uint8_t)40ULL): goto ma101; case ((uint8_t)41ULL): goto ma102; case ((uint8_t)42ULL): goto ma103; case ((uint8_t)43ULL): goto ma104; case ((uint8_t)44ULL): goto ma105; case ((uint8_t)45ULL): goto ma106; case ((uint8_t)46ULL): goto ma106; case ((uint8_t)47ULL): goto ma106; case ((uint8_t)48ULL): goto ma106; case ((uint8_t)49ULL): goto ma106; case ((uint8_t)50ULL): goto ma106; case ((uint8_t)51ULL): goto ma106; case ((uint8_t)52ULL): goto ma106; case ((uint8_t)53ULL): goto ma106; case ((uint8_t)54ULL): goto ma106; case ((uint8_t)55ULL): goto ma106; case ((uint8_t)56ULL): goto ma106; case ((uint8_t)57ULL): goto ma106; case ((uint8_t)58ULL): goto ma106; case ((uint8_t)59ULL): goto ma106; case ((uint8_t)60ULL): goto ma106; case ((uint8_t)61ULL): goto ma106; case ((uint8_t)62ULL): goto ma106; case ((uint8_t)63ULL): goto ma106; case ((uint8_t)64ULL): goto ma106; case ((uint8_t)65ULL): goto ma106; case ((uint8_t)66ULL): goto ma106; case ((uint8_t)67ULL): goto ma106; case ((uint8_t)68ULL): goto ma106; case ((uint8_t)69ULL): goto ma106; case ((uint8_t)70ULL): goto ma106; case ((uint8_t)71ULL): goto ma106; case ((uint8_t)72ULL): goto ma106; case ((uint8_t)73ULL): goto ma106; case ((uint8_t)74ULL): goto ma106; case ((uint8_t)75ULL): goto ma106; case ((uint8_t)76ULL): goto ma106; case ((uint8_t)77ULL): goto ma106; case ((uint8_t)78ULL): goto ma106; case ((uint8_t)79ULL): goto ma106; case ((uint8_t)80ULL): goto ma106; case ((uint8_t)81ULL): goto ma106; default: goto ma106; }
ma101:;
                                    uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    if (t110 != ((uint8_t)40ULL)) goto mn109;
                                    void * t111 = gm_l0;
                                    gm_tl874t2atnlh3 t112 = gm_l1;
                                    gm_t88rcucg1b7uk t113 = gm_l9;
                                    gm_b16_8 t114 = gm_l14;
                                    gm_b16_8 t115 = gm_l15;
                                    gm_t1ec8br9ddkav t116;
                                    gm_f_equal_e5vf82h((void *)&t116, (void *)NULL, cx, NULL, t111, t112, t113, t114, t115);
                                    if (gm_unw.kind) {
                                        goto gm_ll51;
                                    }
                                    gm_t1ec8br9ddkav t117 = t116;
                                    if (((((uint8_t *)&t117))[0] != 0)) {
                                        gm_t76123gfet73g t118 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t117) + 8));
                                        gm_tcfa6l7reoosn t119;
                                        GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
                                        ((uint8_t *)&t119)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t119 + 8), t118);
                                        F.ret = t119;
                                        goto gm_fret;
                                    }
                                    uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&t117) + 8));
                                    t99 = t120;
                                    goto me100;
mn109:;
ma102:;
                                    uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    if (t122 != ((uint8_t)41ULL)) goto mn121;
                                    void * t123 = gm_l0;
                                    gm_tl874t2atnlh3 t124 = gm_l1;
                                    gm_t88rcucg1b7uk t125 = gm_l9;
                                    gm_b16_8 t126 = gm_l14;
                                    gm_b16_8 t127 = gm_l15;
                                    gm_t1ec8br9ddkav t128;
                                    gm_f_equal_e5vf82h((void *)&t128, (void *)NULL, cx, NULL, t123, t124, t125, t126, t127);
                                    if (gm_unw.kind) {
                                        goto gm_ll51;
                                    }
                                    gm_t1ec8br9ddkav t129 = t128;
                                    if (((((uint8_t *)&t129))[0] != 0)) {
                                        gm_t76123gfet73g t130 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t129) + 8));
                                        gm_tcfa6l7reoosn t131;
                                        GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
                                        ((uint8_t *)&t131)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t131 + 8), t130);
                                        F.ret = t131;
                                        goto gm_fret;
                                    }
                                    uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&t129) + 8));
                                    uint8_t t133 = (uint8_t)!t132;
                                    t99 = t133;
                                    goto me100;
mn121:;
ma103:;
                                    uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    if (t135 != ((uint8_t)42ULL)) goto mn134;
                                    void * t136 = gm_l0;
                                    gm_tl874t2atnlh3 t137 = gm_l1;
                                    gm_ts6oqppjh0ba3 t138;
                                    GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)10ULL));
                                    gm_t88rcucg1b7uk t139 = gm_l9;
                                    gm_b16_8 t140 = gm_l14;
                                    gm_b16_8 t141 = gm_l15;
                                    gm_t1ec8br9ddkav t142;
                                    gm_f_cmp_op_mflafbf((void *)&t142, (void *)NULL, cx, NULL, t136, t137, t138, t139, t140, t141);
                                    if (gm_unw.kind) {
                                        goto gm_ll51;
                                    }
                                    gm_t1ec8br9ddkav t143 = t142;
                                    if (((((uint8_t *)&t143))[0] != 0)) {
                                        gm_t76123gfet73g t144 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t143) + 8));
                                        gm_tcfa6l7reoosn t145;
                                        GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t145 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
                                        ((uint8_t *)&t145)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t145 + 8), t144);
                                        F.ret = t145;
                                        goto gm_fret;
                                    }
                                    uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&t143) + 8));
                                    t99 = t146;
                                    goto me100;
mn134:;
ma104:;
                                    uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    if (t148 != ((uint8_t)43ULL)) goto mn147;
                                    void * t149 = gm_l0;
                                    gm_tl874t2atnlh3 t150 = gm_l1;
                                    gm_ts6oqppjh0ba3 t151;
                                    GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t151 + 0), ((uint8_t)11ULL));
                                    gm_t88rcucg1b7uk t152 = gm_l9;
                                    gm_b16_8 t153 = gm_l14;
                                    gm_b16_8 t154 = gm_l15;
                                    gm_t1ec8br9ddkav t155;
                                    gm_f_cmp_op_mflafbf((void *)&t155, (void *)NULL, cx, NULL, t149, t150, t151, t152, t153, t154);
                                    if (gm_unw.kind) {
                                        goto gm_ll51;
                                    }
                                    gm_t1ec8br9ddkav t156 = t155;
                                    if (((((uint8_t *)&t156))[0] != 0)) {
                                        gm_t76123gfet73g t157 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t156) + 8));
                                        gm_tcfa6l7reoosn t158;
                                        GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
                                        ((uint8_t *)&t158)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t158 + 8), t157);
                                        F.ret = t158;
                                        goto gm_fret;
                                    }
                                    uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&t156) + 8));
                                    t99 = t159;
                                    goto me100;
mn147:;
ma105:;
                                    uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    if (t161 != ((uint8_t)44ULL)) goto mn160;
                                    void * t162 = gm_l0;
                                    gm_tl874t2atnlh3 t163 = gm_l1;
                                    gm_ts6oqppjh0ba3 t164;
                                    GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t164 + 0), ((uint8_t)12ULL));
                                    gm_t88rcucg1b7uk t165 = gm_l9;
                                    gm_b16_8 t166 = gm_l14;
                                    gm_b16_8 t167 = gm_l15;
                                    gm_t1ec8br9ddkav t168;
                                    gm_f_cmp_op_mflafbf((void *)&t168, (void *)NULL, cx, NULL, t162, t163, t164, t165, t166, t167);
                                    if (gm_unw.kind) {
                                        goto gm_ll51;
                                    }
                                    gm_t1ec8br9ddkav t169 = t168;
                                    if (((((uint8_t *)&t169))[0] != 0)) {
                                        gm_t76123gfet73g t170 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t169) + 8));
                                        gm_tcfa6l7reoosn t171;
                                        GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
                                        ((uint8_t *)&t171)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t171 + 8), t170);
                                        F.ret = t171;
                                        goto gm_fret;
                                    }
                                    uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&t169) + 8));
                                    t99 = t172;
                                    goto me100;
mn160:;
ma106:;
                                    void * t174 = gm_l0;
                                    gm_tl874t2atnlh3 t175 = gm_l1;
                                    gm_ts6oqppjh0ba3 t176;
                                    GM_ST(uint8_t, (uint8_t *)&t176 + 1, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t176 + 0), ((uint8_t)13ULL));
                                    gm_t88rcucg1b7uk t177 = gm_l9;
                                    gm_b16_8 t178 = gm_l14;
                                    gm_b16_8 t179 = gm_l15;
                                    gm_t1ec8br9ddkav t180;
                                    gm_f_cmp_op_mflafbf((void *)&t180, (void *)NULL, cx, NULL, t174, t175, t176, t177, t178, t179);
                                    if (gm_unw.kind) {
                                        goto gm_ll51;
                                    }
                                    gm_t1ec8br9ddkav t181 = t180;
                                    if (((((uint8_t *)&t181))[0] != 0)) {
                                        gm_t76123gfet73g t182 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t181) + 8));
                                        gm_tcfa6l7reoosn t183;
                                        GM_ST(uint8_t, (uint8_t *)&t183 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t183 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t183 + 4, 0);
                                        ((uint8_t *)&t183)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t183 + 8), t182);
                                        F.ret = t183;
                                        goto gm_fret;
                                    }
                                    uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&t181) + 8));
                                    t99 = t184;
                                    goto me100;
mn173:;
ma107:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 14, 22);
me100:;
                                    gm_l16 = t99;
                                }
                                {
                                    uint64_t t185 = gm_l13;
                                    uint64_t t186 = (uintptr_t)t185;
                                    uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    if (t186 >= (uintptr_t)(t187)) gm_trap_index(t186, t187, gm_srcns60k96jj1mt, 22, 17);
                                    uint8_t *t188 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t186 * 1);
                                    uint8_t t189;
                                    uint8_t t191 = gm_l16;
                                    if (!t191) goto ie190;
                                    uint8_t t193 = ((uint8_t)1ULL);
                                    t189 = t193;
                                    goto ix190;
ie190:;
                                    uint8_t t195 = ((uint8_t)0ULL);
                                    t189 = t195;
ix190:;
                                    GM_ST(uint8_t, t188, t189);
                                }
                                goto me55;
mn56:;
                                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 11, 13);
me55:;
                            }
lc47:;
                            {
                                uint64_t t197 = gm_l12;
                                uint64_t r198; bool o199 = __builtin_add_overflow(t197, ((uint64_t)1ULL), &r198);
                                uint64_t t200 = r198;
                                gm_l12 = t200;
                            }
                        }
gm_ll51:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb47; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc47; }
                        }
                        goto gm_lad;
lb47:;
                    }
                }
                goto ix31;
ie31:;
                {
                    void * t202 = gm_l0;
                    gm_tl874t2atnlh3 t203 = gm_l1;
                    gm_b16_8 t204;
                    GM_ST(void *, (uint8_t *)&t204, (void *)(gm_s4lpfn3di4rp9));
                    GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(65));
                    gm_t76123gfet73g t205;
                    gm_f_trap_qh84l9d((void *)&t205, (void *)NULL, cx, NULL, t202, t203, t204);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t206;
                    GM_ST(uint8_t, (uint8_t *)&t206 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t206 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t206 + 4, 0);
                    ((uint8_t *)&t206)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t206 + 8), t205);
                    gm_tcfa6l7reoosn t207 = t206;
                    uint8_t t208 = ((((uint8_t *)&t207))[0] != 0);
                    F.ret = t206;
                    goto gm_fret;
                }
ix31:;
            }
            goto me3;
mn16:;
ma5:;
            uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t212 != ((uint8_t)46ULL)) goto pa211;
            goto po210;
pa211:;
            uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t214 != ((uint8_t)47ULL)) goto pa213;
            goto po210;
pa213:;
            uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t216 != ((uint8_t)48ULL)) goto pa215;
            goto po210;
pa215:;
            goto mn209;
po210:;
            {
                void * t219 = gm_l0;
                gm_t88rcucg1b7uk t220 = gm_l4;
                gm_t53at80pfdus3 t221;
                gm_f_kind_htv6093((void *)&t221, t219, GM_CONV(gm_csguankko4lmhd, t220));
                gm_t53at80pfdus3 t222 = t221;
                uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&t222) + 0));
                if (t223 != ((uint8_t)11ULL)) goto ie218;
                uint64_t t224 = GM_LD(uint64_t, ((((uint8_t *)&t222) + 8) + 0));
                gm_l17 = t224;
                gm_t88rcucg1b7uk t225 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t222) + 8) + 8));
                gm_l18 = t225;
                {
                    void * t227 = gm_l0;
                    gm_t88rcucg1b7uk t228 = gm_l18;
                    uint64_t t229 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t227, t228);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t230 = gm_f_us_cuv01fp(t229);
                    gm_l19 = t230;
                }
                {
                    void * t231 = gm_l0;
                    gm_t2lqu8jsj511d t232 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t231));
                    gm_l20 = t232;
                }
                {
                    void * t233 = gm_l0;
                    uint64_t t234 = gm_l19;
                    uint64_t t235 = (uint64_t)t234;
                    gm_b16_8 t236 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t233, t235);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l21 = t236;
                }
                {
                    gm_b16_8 t237 = gm_l21;
                    uint64_t t238 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t239 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t238 >= (uintptr_t)(t239)) gm_trap_index(t238, t239, gm_srcns60k96jj1mt, 30, 27);
                    uint64_t t240 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t238 * 16) + 8);
                    void * t241 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t238 * 16)));
                    uint64_t t242 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t243 = gm_l19;
                    uint64_t t244 = (uintptr_t)t243;
                    if (t242 > t244 || t244 > (uintptr_t)t240) gm_trap_slice(t242, t244, t240, gm_srcns60k96jj1mt, 30, 27);
                    gm_b16_8 t245;
                    GM_ST(void *, (uint8_t *)&t245, (void *)(((uint8_t *)t241 + t242 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t245 + 8, (uintptr_t)(t244 - t242));
                    gm_b16_8 t246 = t245;
                    gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)&t246));
                    gm_f_copy_from_f7j6p05(t237, t247);
                }
                {
                    void * t248 = gm_l0;
                    uint64_t t249 = gm_l19;
                    uint64_t t250 = (uint64_t)t249;
                    gm_b16_8 t251 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t248, t250);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l22 = t251;
                }
                {
                    gm_f_list_fufbgf7((void *)&F.l23, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                }
                {
                    {
                        gm_l25 = ((uint64_t)1ULL);
                    }
                    {
                        uint64_t t254 = gm_l17;
                        uint64_t t255 = gm_f_us_cuv01fp(t254);
                        gm_l24 = t255;
                    }
                    {
                        for (;;) {
                            uint64_t t257 = gm_l25;
                            uint64_t t258 = gm_l24;
                            uint8_t t259 = (t257 < t258);
                            if (!t259) goto lb256;
                            {
                                uint64_t t262 = gm_l25;
                                uint64_t t263 = t262;
                                uint64_t t266 = GM_LD(uint64_t, ((uint8_t *)&t263));
                                gm_l26 = t266;
                                {
                                    uint64_t t268 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t269 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    if (t268 >= (uintptr_t)(t269)) gm_trap_index(t268, t269, gm_srcns60k96jj1mt, 35, 22);
                                    uint64_t t270 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t268 * 16) + 8);
                                    void * t271 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t268 * 16)));
                                    uint64_t t272 = gm_l26;
                                    uint64_t t273 = gm_l19;
                                    uint64_t r274; bool o275 = __builtin_mul_overflow(t272, t273, &r274);
                                    if (o275) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 35, 27);
                                    uint64_t t276 = r274;
                                    uint64_t t277 = (uintptr_t)t276;
                                    uint64_t t278 = gm_l26;
                                    uint64_t r279; bool o280 = __builtin_add_overflow(t278, ((uint64_t)1ULL), &r279);
                                    if (o280) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 35, 36);
                                    uint64_t t281 = r279;
                                    uint64_t t282 = gm_l19;
                                    uint64_t r283; bool o284 = __builtin_mul_overflow(t281, t282, &r283);
                                    if (o284) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 35, 35);
                                    uint64_t t285 = r283;
                                    uint64_t t286 = (uintptr_t)t285;
                                    if (t277 > t286 || t286 > (uintptr_t)t270) gm_trap_slice(t277, t286, t270, gm_srcns60k96jj1mt, 35, 22);
                                    gm_b16_8 t287;
                                    GM_ST(void *, (uint8_t *)&t287, (void *)(((uint8_t *)t271 + t277 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t287 + 8, (uintptr_t)(t286 - t277));
                                    gm_l27 = t287;
                                }
                                {
                                    uint8_t t289 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)46ULL)));
                                    if (!t289) goto ie288;
                                    {
                                        void * t291 = gm_l0;
                                        gm_tl874t2atnlh3 t292 = gm_l1;
                                        gm_t9qkh0rsooc8d t293;
                                        GM_ST(uint8_t, ((uint8_t *)&t293 + 0), ((uint8_t)0ULL));
                                        gm_ts6oqppjh0ba3 t294;
                                        GM_ST(uint8_t, ((uint8_t *)&t294 + 0), ((uint8_t)0ULL));
                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t294 + 1), t293);
                                        gm_t88rcucg1b7uk t295 = gm_l18;
                                        gm_t88rcucg1b7uk t296 = gm_l18;
                                        gm_t88rcucg1b7uk t297 = gm_l18;
                                        gm_b16_8 t298 = gm_l21;
                                        gm_b16_8 t299 = gm_l27;
                                        gm_b16_8 t300 = gm_l22;
                                        gm_tcfa6l7reoosn t301;
                                        gm_f_binop_nrsq3mp((void *)&t301, (void *)NULL, cx, NULL, t291, t292, t294, t295, t296, t297, t298, t299, t300);
                                        if (gm_unw.kind) {
                                            goto gm_ll260;
                                        }
                                        gm_tcfa6l7reoosn t302 = t301;
                                        if (((((uint8_t *)&t302))[0] != 0)) {
                                            gm_t76123gfet73g t303 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t302) + 8));
                                            gm_tcfa6l7reoosn t304;
                                            GM_ST(uint8_t, (uint8_t *)&t304 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t304 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t304 + 4, 0);
                                            ((uint8_t *)&t304)[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t304 + 8), t303);
                                            F.ret = t304;
                                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                                            goto gm_ll260;
                                        }
                                        gm_b0_1 t305 = GM_LD(gm_b0_1, (((uint8_t *)&t302) + 8));
                                    }
                                    goto ix288;
ie288:;
                                    {
                                        void * t307 = (void *)((uint8_t *)&F.l23);
                                        gm_f_truncate_j7f0pmi(t307, ((uint64_t)0ULL));
                                    }
                                    {
                                        void * t308 = (void *)((uint8_t *)&F.l23);
                                        gm_b16_8 t309 = gm_l21;
                                        gm_tftb1odc739t5 t310 = gm_f_push_rluch9r(t308, t309);
                                        gm_tftb1odc739t5 t311 = t310;
                                        if (((((uint8_t *)&t311))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t311) + 1, gm_srcns60k96jj1mt, 40, 21);
                                        gm_b0_1 t312 = GM_LD(gm_b0_1, (((uint8_t *)&t311) + 1));
                                    }
                                    {
                                        void * t313 = (void *)((uint8_t *)&F.l23);
                                        gm_b16_8 t314 = gm_l27;
                                        gm_tftb1odc739t5 t315 = gm_f_push_rluch9r(t313, t314);
                                        gm_tftb1odc739t5 t316 = t315;
                                        if (((((uint8_t *)&t316))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t316) + 1, gm_srcns60k96jj1mt, 41, 21);
                                        gm_b0_1 t317 = GM_LD(gm_b0_1, (((uint8_t *)&t316) + 1));
                                    }
                                    {
                                        gm_tvdg94bk7aa22 t318;
                                        uint8_t t320 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)47ULL)));
                                        if (!t320) goto ie319;
                                        gm_tvdg94bk7aa22 t322;
                                        GM_ST(uint8_t, (uint8_t *)&t322 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t322 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t322 + 4, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t322 + 8, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t322 + 0), ((uint8_t)16ULL));
                                        gm_tvdg94bk7aa22 t323 = t322;
                                        t318 = t323;
                                        goto ix319;
ie319:;
                                        gm_tvdg94bk7aa22 t325;
                                        GM_ST(uint8_t, (uint8_t *)&t325 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t325 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t325 + 4, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t325 + 8, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t325 + 0), ((uint8_t)17ULL));
                                        gm_tvdg94bk7aa22 t326 = t325;
                                        t318 = t326;
ix319:;
                                        gm_l28 = t318;
                                    }
                                    {
                                        void * t327 = gm_l0;
                                        gm_tl874t2atnlh3 t328 = gm_l1;
                                        gm_tvdg94bk7aa22 t329 = gm_l28;
                                        gm_t88rcucg1b7uk t330 = gm_l18;
                                        gm_t88rcucg1b7uk t331 = gm_l18;
                                        gm_t88rcucg1b7uk t332 = gm_l18;
                                        gm_b16_8 t333 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
                                        gm_b16_8 t334 = gm_l22;
                                        gm_tcfa6l7reoosn t335;
                                        gm_f_apply_builtin_41dffti((void *)&t335, (void *)NULL, cx, NULL, t327, t328, t329, t330, t331, t332, t333, t334);
                                        if (gm_unw.kind) {
                                            goto gm_ll260;
                                        }
                                        gm_tcfa6l7reoosn t336 = t335;
                                        if (((((uint8_t *)&t336))[0] != 0)) {
                                            gm_t76123gfet73g t337 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t336) + 8));
                                            gm_tcfa6l7reoosn t338;
                                            GM_ST(uint8_t, (uint8_t *)&t338 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t338 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t338 + 4, 0);
                                            ((uint8_t *)&t338)[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t338 + 8), t337);
                                            F.ret = t338;
                                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                                            goto gm_ll260;
                                        }
                                        gm_b0_1 t339 = GM_LD(gm_b0_1, (((uint8_t *)&t336) + 8));
                                    }
ix288:;
                                }
                                {
                                    gm_b16_8 t340 = gm_l21;
                                    gm_b16_8 t341 = gm_l22;
                                    gm_b16_8 t342 = t341;
                                    gm_b16_8 t343 = GM_LD(gm_b16_8, ((uint8_t *)&t342));
                                    gm_f_copy_from_f7j6p05(t340, t343);
                                }
                                goto me264;
mn265:;
                                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 34, 13);
me264:;
                            }
lc256:;
                            {
                                uint64_t t345 = gm_l25;
                                uint64_t r346; bool o347 = __builtin_add_overflow(t345, ((uint64_t)1ULL), &r346);
                                uint64_t t348 = r346;
                                gm_l25 = t348;
                            }
                        }
gm_ll260:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb256; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc256; }
                        }
                        goto gm_ld226_1;
lb256:;
                    }
                }
                {
                    gm_b16_8 t349 = gm_l7;
                    gm_b16_8 t350 = gm_l21;
                    gm_b16_8 t351 = t350;
                    gm_b16_8 t352 = GM_LD(gm_b16_8, ((uint8_t *)&t351));
                    gm_f_copy_from_f7j6p05(t349, t352);
                }
                {
                    void * t353 = gm_l0;
                    gm_t2lqu8jsj511d t354 = gm_l20;
                    gm_f_release_jn5kqdi(t353, GM_CONV(gm_csc7lc6jlkqkuq, t354));
                }
                {
                    void * t355 = (void *)((uint8_t *)&F.l23);
                    gm_f_deinit_tq06jtm(t355);
                }
                goto gm_ls356;
gm_ld226_1:;
                {
                    gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                    void * t357 = (void *)((uint8_t *)&F.l23);
                    gm_f_deinit_tq06jtm(t357);
                    gm_unw = gm_saved;
                }
                goto gm_lad;
gm_ls356:;
                goto ix218;
ie218:;
                {
                    void * t359 = gm_l0;
                    gm_tl874t2atnlh3 t360 = gm_l1;
                    gm_b16_8 t361;
                    GM_ST(void *, (uint8_t *)&t361, (void *)(gm_sm94b3smoinpd));
                    GM_ST(uintptr_t, (uint8_t *)&t361 + 8, (uintptr_t)(59));
                    gm_t76123gfet73g t362;
                    gm_f_trap_qh84l9d((void *)&t362, (void *)NULL, cx, NULL, t359, t360, t361);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t363;
                    GM_ST(uint8_t, (uint8_t *)&t363 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t363 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t363 + 4, 0);
                    ((uint8_t *)&t363)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t363 + 8), t362);
                    gm_tcfa6l7reoosn t364 = t363;
                    uint8_t t365 = ((((uint8_t *)&t364))[0] != 0);
                    F.ret = t363;
                    goto gm_fret;
                }
ix218:;
            }
            goto me3;
mn209:;
ma6:;
            uint8_t t367 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t367 != ((uint8_t)49ULL)) goto mn366;
            {
                gm_l29 = ((uint8_t)1);
            }
            {
                {
                    uint64_t t370 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t371 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t370 >= (uintptr_t)(t371)) gm_trap_index(t370, t371, gm_srcns60k96jj1mt, 52, 22);
                    gm_b16_8 t372 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t370 * 16));
                    gm_l30 = t372;
                }
                {
                    gm_l31 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t374 = gm_l31;
                        uint64_t t375 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                        uint8_t t376 = (t374 < t375);
                        if (!t376) goto lb373;
                        {
                            uint64_t t379 = gm_l31;
                            uint64_t t380 = (uintptr_t)t379;
                            uint64_t t381 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                            uint8_t t382 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t380 * 1));
                            uint8_t t383 = t382;
                            uint8_t t386 = GM_LD(uint8_t, ((uint8_t *)&t383));
                            gm_l32 = t386;
                            {
                                uint8_t t389 = gm_l32;
                                uint8_t t390 = ((t389 == ((uint8_t)0ULL)));
                                if (!t390) goto ie388;
                                {
                                    gm_l29 = ((uint8_t)0);
                                }
                                goto ix388;
ie388:;
ix388:;
                            }
                            goto me384;
mn385:;
                            gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 52, 13);
me384:;
                        }
lc373:;
                        {
                            uint64_t t393 = gm_l31;
                            uint64_t r394; bool o395 = __builtin_add_overflow(t393, ((uint64_t)1ULL), &r394);
                            uint64_t t396 = r394;
                            gm_l31 = t396;
                        }
                    }
lb373:;
                }
            }
            {
                uint64_t t397 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t398 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                if (t397 >= (uintptr_t)(t398)) gm_trap_index(t397, t398, gm_srcns60k96jj1mt, 55, 13);
                uint8_t *t399 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t397 * 1);
                uint8_t t400;
                uint8_t t402 = gm_l29;
                if (!t402) goto ie401;
                uint8_t t404 = ((uint8_t)1ULL);
                t400 = t404;
                goto ix401;
ie401:;
                uint8_t t406 = ((uint8_t)0ULL);
                t400 = t406;
ix401:;
                GM_ST(uint8_t, t399, t400);
            }
            goto me3;
mn366:;
ma7:;
            uint8_t t408 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t408 != ((uint8_t)50ULL)) goto mn407;
            {
                gm_l33 = ((uint8_t)0);
            }
            {
                {
                    uint64_t t411 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t412 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t411 >= (uintptr_t)(t412)) gm_trap_index(t411, t412, gm_srcns60k96jj1mt, 59, 22);
                    gm_b16_8 t413 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t411 * 16));
                    gm_l34 = t413;
                }
                {
                    gm_l35 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t415 = gm_l35;
                        uint64_t t416 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                        uint8_t t417 = (t415 < t416);
                        if (!t417) goto lb414;
                        {
                            uint64_t t420 = gm_l35;
                            uint64_t t421 = (uintptr_t)t420;
                            uint64_t t422 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                            uint8_t t423 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t421 * 1));
                            uint8_t t424 = t423;
                            uint8_t t427 = GM_LD(uint8_t, ((uint8_t *)&t424));
                            gm_l36 = t427;
                            {
                                uint8_t t430 = gm_l36;
                                uint8_t t431 = (!(t430 == ((uint8_t)0ULL)));
                                if (!t431) goto ie429;
                                {
                                    gm_l33 = ((uint8_t)1);
                                }
                                goto ix429;
ie429:;
ix429:;
                            }
                            goto me425;
mn426:;
                            gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 59, 13);
me425:;
                        }
lc414:;
                        {
                            uint64_t t434 = gm_l35;
                            uint64_t r435; bool o436 = __builtin_add_overflow(t434, ((uint64_t)1ULL), &r435);
                            uint64_t t437 = r435;
                            gm_l35 = t437;
                        }
                    }
lb414:;
                }
            }
            {
                uint64_t t438 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t439 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                if (t438 >= (uintptr_t)(t439)) gm_trap_index(t438, t439, gm_srcns60k96jj1mt, 62, 13);
                uint8_t *t440 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t438 * 1);
                uint8_t t441;
                uint8_t t443 = gm_l33;
                if (!t443) goto ie442;
                uint8_t t445 = ((uint8_t)1ULL);
                t441 = t445;
                goto ix442;
ie442:;
                uint8_t t447 = ((uint8_t)0ULL);
                t441 = t447;
ix442:;
                GM_ST(uint8_t, t440, t441);
            }
            goto me3;
mn407:;
ma8:;
            uint8_t t449 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t449 != ((uint8_t)51ULL)) goto mn448;
            {
                gm_l37 = ((uint64_t)0ULL);
            }
            {
                {
                    uint64_t t452 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t453 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t452 >= (uintptr_t)(t453)) gm_trap_index(t452, t453, gm_srcns60k96jj1mt, 66, 22);
                    gm_b16_8 t454 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t452 * 16));
                    gm_l38 = t454;
                }
                {
                    gm_l39 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t456 = gm_l39;
                        uint64_t t457 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                        uint8_t t458 = (t456 < t457);
                        if (!t458) goto lb455;
                        {
                            uint64_t t461 = gm_l39;
                            uint64_t t462 = (uintptr_t)t461;
                            uint64_t t463 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                            uint8_t t464 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t462 * 1));
                            uint8_t t465 = t464;
                            uint8_t t468 = GM_LD(uint8_t, ((uint8_t *)&t465));
                            gm_l40 = t468;
                            {
                                uint8_t t471 = gm_l40;
                                uint8_t t472 = (!(t471 == ((uint8_t)0ULL)));
                                if (!t472) goto ie470;
                                {
                                    uint64_t t474 = gm_l37;
                                    uint64_t r475; bool o476 = __builtin_add_overflow(t474, ((uint64_t)1ULL), &r475);
                                    if (o476) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 67, 33);
                                    uint64_t t477 = r475;
                                    gm_l37 = t477;
                                }
                                goto ix470;
ie470:;
ix470:;
                            }
                            goto me466;
mn467:;
                            gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 66, 13);
me466:;
                        }
lc455:;
                        {
                            uint64_t t479 = gm_l39;
                            uint64_t r480; bool o481 = __builtin_add_overflow(t479, ((uint64_t)1ULL), &r480);
                            uint64_t t482 = r480;
                            gm_l39 = t482;
                        }
                    }
lb455:;
                }
            }
            {
                void * t483 = gm_l0;
                gm_b16_8 t484 = gm_l7;
                uint64_t t485 = gm_l37;
                unsigned __int128 t486 = (unsigned __int128)t485;
                gm_f_put_uint_cddin99(t483, t484, t486);
            }
            goto me3;
mn448:;
ma9:;
            uint8_t t488 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t488 != ((uint8_t)52ULL)) goto mn487;
            {
                void * t491 = gm_l0;
                gm_t88rcucg1b7uk t492 = gm_l5;
                gm_t53at80pfdus3 t493;
                gm_f_kind_htv6093((void *)&t493, t491, GM_CONV(gm_csguankko4lmhd, t492));
                gm_t53at80pfdus3 t494 = t493;
                uint8_t t495 = GM_LD(uint8_t, (((uint8_t *)&t494) + 0));
                if (t495 != ((uint8_t)11ULL)) goto ie490;
                uint64_t t496 = GM_LD(uint64_t, ((((uint8_t *)&t494) + 8) + 0));
                gm_l41 = t496;
                gm_t88rcucg1b7uk t497 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t494) + 8) + 8));
                gm_l42 = t497;
                {
                    void * t499 = gm_l0;
                    gm_t88rcucg1b7uk t500 = gm_l42;
                    uint64_t t501 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t499, t500);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t502 = gm_f_us_cuv01fp(t501);
                    gm_l43 = t502;
                }
                {
                    gm_b16_8 t503 = gm_l7;
                    uint64_t t504 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t505 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t504 >= (uintptr_t)(t505)) gm_trap_index(t504, t505, gm_srcns60k96jj1mt, 74, 27);
                    gm_b16_8 t506 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t504 * 16));
                    gm_b16_8 t507 = t506;
                    gm_b16_8 t508 = GM_LD(gm_b16_8, ((uint8_t *)&t507));
                    gm_f_copy_from_f7j6p05(t503, t508);
                }
                {
                    {
                        gm_l45 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t510 = gm_l41;
                        uint64_t t511 = gm_f_us_cuv01fp(t510);
                        gm_l44 = t511;
                    }
                    {
                        for (;;) {
                            uint64_t t513 = gm_l45;
                            uint64_t t514 = gm_l44;
                            uint8_t t515 = (t513 < t514);
                            if (!t515) goto lb512;
                            {
                                uint64_t t518 = gm_l45;
                                uint64_t t519 = t518;
                                uint64_t t522 = GM_LD(uint64_t, ((uint8_t *)&t519));
                                gm_l46 = t522;
                                {
                                    uint64_t t525 = gm_l46;
                                    uint64_t t526 = (uintptr_t)t525;
                                    uint64_t t527 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t528 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    if (t527 >= (uintptr_t)(t528)) gm_trap_index(t527, t528, gm_srcns60k96jj1mt, 76, 20);
                                    uint64_t t529 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t527 * 16) + 8);
                                    if (t526 >= (uintptr_t)(t529)) gm_trap_index(t526, t529, gm_srcns60k96jj1mt, 76, 20);
                                    uint8_t t530 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t527 * 16)) + t526 * 1));
                                    uint8_t t531 = (!(t530 == ((uint8_t)0ULL)));
                                    if (!t531) goto ie524;
                                    {
                                        uint64_t t533 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                        void * t534 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                                        uint64_t t535 = gm_l46;
                                        uint64_t t536 = gm_l43;
                                        uint64_t r537; bool o538 = __builtin_mul_overflow(t535, t536, &r537);
                                        if (o538) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 76, 43);
                                        uint64_t t539 = r537;
                                        uint64_t t540 = (uintptr_t)t539;
                                        uint64_t t541 = gm_l46;
                                        uint64_t r542; bool o543 = __builtin_add_overflow(t541, ((uint64_t)1ULL), &r542);
                                        if (o543) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 76, 52);
                                        uint64_t t544 = r542;
                                        uint64_t t545 = gm_l43;
                                        uint64_t r546; bool o547 = __builtin_mul_overflow(t544, t545, &r546);
                                        if (o547) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 76, 51);
                                        uint64_t t548 = r546;
                                        uint64_t t549 = (uintptr_t)t548;
                                        if (t540 > t549 || t549 > (uintptr_t)t533) gm_trap_slice(t540, t549, t533, gm_srcns60k96jj1mt, 76, 39);
                                        gm_b16_8 t550;
                                        GM_ST(void *, (uint8_t *)&t550, (void *)(((uint8_t *)t534 + t540 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t550 + 8, (uintptr_t)(t549 - t540));
                                        uint64_t t551 = (uintptr_t)((uint64_t)1ULL);
                                        uint64_t t552 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                        if (t551 >= (uintptr_t)(t552)) gm_trap_index(t551, t552, gm_srcns60k96jj1mt, 76, 75);
                                        uint64_t t553 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t551 * 16) + 8);
                                        void * t554 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t551 * 16)));
                                        uint64_t t555 = gm_l46;
                                        uint64_t t556 = gm_l43;
                                        uint64_t r557; bool o558 = __builtin_mul_overflow(t555, t556, &r557);
                                        if (o558) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 76, 80);
                                        uint64_t t559 = r557;
                                        uint64_t t560 = (uintptr_t)t559;
                                        uint64_t t561 = gm_l46;
                                        uint64_t r562; bool o563 = __builtin_add_overflow(t561, ((uint64_t)1ULL), &r562);
                                        if (o563) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 76, 89);
                                        uint64_t t564 = r562;
                                        uint64_t t565 = gm_l43;
                                        uint64_t r566; bool o567 = __builtin_mul_overflow(t564, t565, &r566);
                                        if (o567) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 76, 88);
                                        uint64_t t568 = r566;
                                        uint64_t t569 = (uintptr_t)t568;
                                        if (t560 > t569 || t569 > (uintptr_t)t553) gm_trap_slice(t560, t569, t553, gm_srcns60k96jj1mt, 76, 75);
                                        gm_b16_8 t570;
                                        GM_ST(void *, (uint8_t *)&t570, (void *)(((uint8_t *)t554 + t560 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t570 + 8, (uintptr_t)(t569 - t560));
                                        gm_b16_8 t571 = t570;
                                        gm_b16_8 t572 = GM_LD(gm_b16_8, ((uint8_t *)&t571));
                                        gm_f_copy_from_f7j6p05(t550, t572);
                                    }
                                    goto ix524;
ie524:;
ix524:;
                                }
                                goto me520;
mn521:;
                                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 75, 13);
me520:;
                            }
lc512:;
                            {
                                uint64_t t574 = gm_l45;
                                uint64_t r575; bool o576 = __builtin_add_overflow(t574, ((uint64_t)1ULL), &r575);
                                uint64_t t577 = r575;
                                gm_l45 = t577;
                            }
                        }
lb512:;
                    }
                }
                goto ix490;
ie490:;
                {
                    void * t579 = gm_l0;
                    gm_tl874t2atnlh3 t580 = gm_l1;
                    gm_b16_8 t581;
                    GM_ST(void *, (uint8_t *)&t581, (void *)(gm_spqlqjii789ln));
                    GM_ST(uintptr_t, (uint8_t *)&t581 + 8, (uintptr_t)(56));
                    gm_t76123gfet73g t582;
                    gm_f_trap_qh84l9d((void *)&t582, (void *)NULL, cx, NULL, t579, t580, t581);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t583;
                    GM_ST(uint8_t, (uint8_t *)&t583 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t583 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t583 + 4, 0);
                    ((uint8_t *)&t583)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t583 + 8), t582);
                    gm_tcfa6l7reoosn t584 = t583;
                    uint8_t t585 = ((((uint8_t *)&t584))[0] != 0);
                    F.ret = t583;
                    goto gm_fret;
                }
ix490:;
            }
            goto me3;
mn487:;
ma10:;
            uint8_t t589 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t589 != ((uint8_t)53ULL)) goto pa588;
            goto po587;
pa588:;
            uint8_t t591 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t591 != ((uint8_t)54ULL)) goto pa590;
            goto po587;
pa590:;
            uint8_t t593 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t593 != ((uint8_t)55ULL)) goto pa592;
            goto po587;
pa592:;
            goto mn586;
po587:;
            {
                void * t596 = gm_l0;
                gm_t88rcucg1b7uk t597 = gm_l4;
                gm_t53at80pfdus3 t598;
                gm_f_kind_htv6093((void *)&t598, t596, GM_CONV(gm_csguankko4lmhd, t597));
                gm_t53at80pfdus3 t599 = t598;
                uint8_t t600 = GM_LD(uint8_t, (((uint8_t *)&t599) + 0));
                if (t600 != ((uint8_t)11ULL)) goto ie595;
                uint64_t t601 = GM_LD(uint64_t, ((((uint8_t *)&t599) + 8) + 0));
                gm_l47 = t601;
                gm_t88rcucg1b7uk t602 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t599) + 8) + 8));
                gm_l48 = t602;
                {
                    void * t604 = gm_l0;
                    gm_t88rcucg1b7uk t605 = gm_l48;
                    uint64_t t606 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t604, t605);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t607 = gm_f_us_cuv01fp(t606);
                    gm_l49 = t607;
                }
                {
                    gm_tvdg94bk7aa22 t608;
                    uint8_t t614 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    switch (t614) { case ((uint8_t)0ULL): goto ma612; case ((uint8_t)1ULL): goto ma612; case ((uint8_t)2ULL): goto ma612; case ((uint8_t)3ULL): goto ma612; case ((uint8_t)4ULL): goto ma612; case ((uint8_t)5ULL): goto ma612; case ((uint8_t)6ULL): goto ma612; case ((uint8_t)7ULL): goto ma612; case ((uint8_t)8ULL): goto ma612; case ((uint8_t)9ULL): goto ma612; case ((uint8_t)10ULL): goto ma612; case ((uint8_t)11ULL): goto ma612; case ((uint8_t)12ULL): goto ma612; case ((uint8_t)13ULL): goto ma612; case ((uint8_t)14ULL): goto ma612; case ((uint8_t)15ULL): goto ma612; case ((uint8_t)16ULL): goto ma612; case ((uint8_t)17ULL): goto ma612; case ((uint8_t)18ULL): goto ma612; case ((uint8_t)19ULL): goto ma612; case ((uint8_t)20ULL): goto ma612; case ((uint8_t)21ULL): goto ma612; case ((uint8_t)22ULL): goto ma612; case ((uint8_t)23ULL): goto ma612; case ((uint8_t)24ULL): goto ma612; case ((uint8_t)25ULL): goto ma612; case ((uint8_t)26ULL): goto ma612; case ((uint8_t)27ULL): goto ma612; case ((uint8_t)28ULL): goto ma612; case ((uint8_t)29ULL): goto ma612; case ((uint8_t)30ULL): goto ma612; case ((uint8_t)31ULL): goto ma612; case ((uint8_t)32ULL): goto ma612; case ((uint8_t)33ULL): goto ma612; case ((uint8_t)34ULL): goto ma612; case ((uint8_t)35ULL): goto ma612; case ((uint8_t)36ULL): goto ma612; case ((uint8_t)37ULL): goto ma612; case ((uint8_t)38ULL): goto ma612; case ((uint8_t)39ULL): goto ma612; case ((uint8_t)40ULL): goto ma612; case ((uint8_t)41ULL): goto ma612; case ((uint8_t)42ULL): goto ma612; case ((uint8_t)43ULL): goto ma612; case ((uint8_t)44ULL): goto ma612; case ((uint8_t)45ULL): goto ma612; case ((uint8_t)46ULL): goto ma612; case ((uint8_t)47ULL): goto ma612; case ((uint8_t)48ULL): goto ma612; case ((uint8_t)49ULL): goto ma612; case ((uint8_t)50ULL): goto ma612; case ((uint8_t)51ULL): goto ma612; case ((uint8_t)52ULL): goto ma612; case ((uint8_t)53ULL): goto ma610; case ((uint8_t)54ULL): goto ma611; case ((uint8_t)55ULL): goto ma612; case ((uint8_t)56ULL): goto ma612; case ((uint8_t)57ULL): goto ma612; case ((uint8_t)58ULL): goto ma612; case ((uint8_t)59ULL): goto ma612; case ((uint8_t)60ULL): goto ma612; case ((uint8_t)61ULL): goto ma612; case ((uint8_t)62ULL): goto ma612; case ((uint8_t)63ULL): goto ma612; case ((uint8_t)64ULL): goto ma612; case ((uint8_t)65ULL): goto ma612; case ((uint8_t)66ULL): goto ma612; case ((uint8_t)67ULL): goto ma612; case ((uint8_t)68ULL): goto ma612; case ((uint8_t)69ULL): goto ma612; case ((uint8_t)70ULL): goto ma612; case ((uint8_t)71ULL): goto ma612; case ((uint8_t)72ULL): goto ma612; case ((uint8_t)73ULL): goto ma612; case ((uint8_t)74ULL): goto ma612; case ((uint8_t)75ULL): goto ma612; case ((uint8_t)76ULL): goto ma612; case ((uint8_t)77ULL): goto ma612; case ((uint8_t)78ULL): goto ma612; case ((uint8_t)79ULL): goto ma612; case ((uint8_t)80ULL): goto ma612; case ((uint8_t)81ULL): goto ma612; default: goto ma612; }
ma610:;
                    uint8_t t616 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t616 != ((uint8_t)53ULL)) goto mn615;
                    gm_tvdg94bk7aa22 t617;
                    GM_ST(uint8_t, (uint8_t *)&t617 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t617 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t617 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t617 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t617 + 0), ((uint8_t)16ULL));
                    t608 = t617;
                    goto me609;
mn615:;
ma611:;
                    uint8_t t619 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t619 != ((uint8_t)54ULL)) goto mn618;
                    gm_tvdg94bk7aa22 t620;
                    GM_ST(uint8_t, (uint8_t *)&t620 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t620 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t620 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t620 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t620 + 0), ((uint8_t)17ULL));
                    t608 = t620;
                    goto me609;
mn618:;
ma612:;
                    gm_tvdg94bk7aa22 t622;
                    GM_ST(uint8_t, (uint8_t *)&t622 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t622 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t622 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t622 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t622 + 0), ((uint8_t)19ULL));
                    t608 = t622;
                    goto me609;
mn621:;
ma613:;
                    gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 82, 35);
me609:;
                    gm_l50 = t608;
                }
                {
                    gm_f_list_fufbgf7((void *)&F.l51, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                }
                {
                    {
                        gm_l53 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t625 = gm_l47;
                        uint64_t t626 = gm_f_us_cuv01fp(t625);
                        gm_l52 = t626;
                    }
                    {
                        for (;;) {
                            uint64_t t628 = gm_l53;
                            uint64_t t629 = gm_l52;
                            uint8_t t630 = (t628 < t629);
                            if (!t630) goto lb627;
                            {
                                uint64_t t633 = gm_l53;
                                uint64_t t634 = t633;
                                uint64_t t637 = GM_LD(uint64_t, ((uint8_t *)&t634));
                                gm_l54 = t637;
                                {
                                    void * t639 = (void *)((uint8_t *)&F.l51);
                                    gm_f_truncate_j7f0pmi(t639, ((uint64_t)0ULL));
                                }
                                {
                                    {
                                        gm_b16_8 t641 = gm_l6;
                                        gm_l55 = t641;
                                    }
                                    {
                                        gm_l56 = ((uint64_t)0ULL);
                                    }
                                    {
                                        for (;;) {
                                            uint64_t t643 = gm_l56;
                                            uint64_t t644 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                                            uint8_t t645 = (t643 < t644);
                                            if (!t645) goto lb642;
                                            {
                                                uint64_t t648 = gm_l56;
                                                uint64_t t649 = (uintptr_t)t648;
                                                uint64_t t650 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                                                gm_b16_8 t653 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l55)) + t649 * 16));
                                                gm_l57 = t653;
                                                {
                                                    void * t655 = (void *)((uint8_t *)&F.l51);
                                                    uint64_t t656 = GM_LD(uintptr_t, ((uint8_t *)&gm_l57) + 8);
                                                    void * t657 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l57)));
                                                    uint64_t t658 = gm_l54;
                                                    uint64_t t659 = gm_l49;
                                                    uint64_t r660; bool o661 = __builtin_mul_overflow(t658, t659, &r660);
                                                    if (o661) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 91, 42);
                                                    uint64_t t662 = r660;
                                                    uint64_t t663 = (uintptr_t)t662;
                                                    uint64_t t664 = gm_l54;
                                                    uint64_t r665; bool o666 = __builtin_add_overflow(t664, ((uint64_t)1ULL), &r665);
                                                    if (o666) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 91, 51);
                                                    uint64_t t667 = r665;
                                                    uint64_t t668 = gm_l49;
                                                    uint64_t r669; bool o670 = __builtin_mul_overflow(t667, t668, &r669);
                                                    if (o670) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 91, 50);
                                                    uint64_t t671 = r669;
                                                    uint64_t t672 = (uintptr_t)t671;
                                                    if (t663 > t672 || t672 > (uintptr_t)t656) gm_trap_slice(t663, t672, t656, gm_srcns60k96jj1mt, 91, 40);
                                                    gm_b16_8 t673;
                                                    GM_ST(void *, (uint8_t *)&t673, (void *)(((uint8_t *)t657 + t663 * 1)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t673 + 8, (uintptr_t)(t672 - t663));
                                                    gm_tftb1odc739t5 t674 = gm_f_push_rluch9r(t655, t673);
                                                    gm_tftb1odc739t5 t675 = t674;
                                                    if (((((uint8_t *)&t675))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t675) + 1, gm_srcns60k96jj1mt, 91, 30);
                                                    gm_b0_1 t676 = GM_LD(gm_b0_1, (((uint8_t *)&t675) + 1));
                                                }
                                                goto me651;
mn652:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 91, 17);
me651:;
                                            }
lc642:;
                                            {
                                                uint64_t t678 = gm_l56;
                                                uint64_t r679; bool o680 = __builtin_add_overflow(t678, ((uint64_t)1ULL), &r679);
                                                uint64_t t681 = r679;
                                                gm_l56 = t681;
                                            }
                                        }
lb642:;
                                    }
                                }
                                {
                                    void * t682 = gm_l0;
                                    gm_tl874t2atnlh3 t683 = gm_l1;
                                    gm_tvdg94bk7aa22 t684 = gm_l50;
                                    gm_t88rcucg1b7uk t685 = gm_l48;
                                    gm_t88rcucg1b7uk t686 = gm_l48;
                                    gm_t88rcucg1b7uk t687 = gm_l48;
                                    gm_b16_8 t688 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
                                    uint64_t t689 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    void * t690 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                                    uint64_t t691 = gm_l54;
                                    uint64_t t692 = gm_l49;
                                    uint64_t r693; bool o694 = __builtin_mul_overflow(t691, t692, &r693);
                                    if (o694) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 92, 85);
                                    uint64_t t695 = r693;
                                    uint64_t t696 = (uintptr_t)t695;
                                    uint64_t t697 = gm_l54;
                                    uint64_t r698; bool o699 = __builtin_add_overflow(t697, ((uint64_t)1ULL), &r698);
                                    if (o699) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 92, 94);
                                    uint64_t t700 = r698;
                                    uint64_t t701 = gm_l49;
                                    uint64_t r702; bool o703 = __builtin_mul_overflow(t700, t701, &r702);
                                    if (o703) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 92, 93);
                                    uint64_t t704 = r702;
                                    uint64_t t705 = (uintptr_t)t704;
                                    if (t696 > t705 || t705 > (uintptr_t)t689) gm_trap_slice(t696, t705, t689, gm_srcns60k96jj1mt, 92, 81);
                                    gm_b16_8 t706;
                                    GM_ST(void *, (uint8_t *)&t706, (void *)(((uint8_t *)t690 + t696 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t706 + 8, (uintptr_t)(t705 - t696));
                                    gm_tcfa6l7reoosn t707;
                                    gm_f_apply_builtin_41dffti((void *)&t707, (void *)NULL, cx, NULL, t682, t683, t684, t685, t686, t687, t688, t706);
                                    if (gm_unw.kind) {
                                        goto gm_ll631;
                                    }
                                    gm_tcfa6l7reoosn t708 = t707;
                                    if (((((uint8_t *)&t708))[0] != 0)) {
                                        gm_t76123gfet73g t709 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t708) + 8));
                                        gm_tcfa6l7reoosn t710;
                                        GM_ST(uint8_t, (uint8_t *)&t710 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t710 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t710 + 4, 0);
                                        ((uint8_t *)&t710)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t710 + 8), t709);
                                        F.ret = t710;
                                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                                        goto gm_ll631;
                                    }
                                    gm_b0_1 t711 = GM_LD(gm_b0_1, (((uint8_t *)&t708) + 8));
                                }
                                goto me635;
mn636:;
                                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 89, 13);
me635:;
                            }
lc627:;
                            {
                                uint64_t t713 = gm_l53;
                                uint64_t r714; bool o715 = __builtin_add_overflow(t713, ((uint64_t)1ULL), &r714);
                                uint64_t t716 = r714;
                                gm_l53 = t716;
                            }
                        }
gm_ll631:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb627; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc627; }
                        }
                        goto gm_ld603_1;
lb627:;
                    }
                }
                {
                    void * t717 = (void *)((uint8_t *)&F.l51);
                    gm_f_deinit_tq06jtm(t717);
                }
                goto gm_ls718;
gm_ld603_1:;
                {
                    gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                    void * t719 = (void *)((uint8_t *)&F.l51);
                    gm_f_deinit_tq06jtm(t719);
                    gm_unw = gm_saved;
                }
                goto gm_lad;
gm_ls718:;
                goto ix595;
ie595:;
                {
                    void * t721 = gm_l0;
                    gm_tl874t2atnlh3 t722 = gm_l1;
                    gm_b16_8 t723;
                    GM_ST(void *, (uint8_t *)&t723, (void *)(gm_scqqkvgm1j3cs));
                    GM_ST(uintptr_t, (uint8_t *)&t723 + 8, (uintptr_t)(64));
                    gm_t76123gfet73g t724;
                    gm_f_trap_qh84l9d((void *)&t724, (void *)NULL, cx, NULL, t721, t722, t723);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t725;
                    GM_ST(uint8_t, (uint8_t *)&t725 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t725 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t725 + 4, 0);
                    ((uint8_t *)&t725)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t725 + 8), t724);
                    gm_tcfa6l7reoosn t726 = t725;
                    uint8_t t727 = ((((uint8_t *)&t726))[0] != 0);
                    F.ret = t725;
                    goto gm_fret;
                }
ix595:;
            }
            goto me3;
mn586:;
ma11:;
            uint8_t t729 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t729 != ((uint8_t)56ULL)) goto mn728;
            {
                void * t732 = gm_l0;
                gm_t88rcucg1b7uk t733 = gm_l4;
                gm_t53at80pfdus3 t734;
                gm_f_kind_htv6093((void *)&t734, t732, GM_CONV(gm_csguankko4lmhd, t733));
                gm_t53at80pfdus3 t735 = t734;
                uint8_t t736 = GM_LD(uint8_t, (((uint8_t *)&t735) + 0));
                if (t736 != ((uint8_t)11ULL)) goto ie731;
                uint64_t t737 = GM_LD(uint64_t, ((((uint8_t *)&t735) + 8) + 0));
                gm_l58 = t737;
                gm_t88rcucg1b7uk t738 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t735) + 8) + 8));
                gm_l59 = t738;
                {
                    void * t741 = gm_l0;
                    gm_t88rcucg1b7uk t742 = gm_l3;
                    gm_t53at80pfdus3 t743;
                    gm_f_kind_htv6093((void *)&t743, t741, GM_CONV(gm_csguankko4lmhd, t742));
                    gm_t53at80pfdus3 t744 = t743;
                    uint8_t t745 = GM_LD(uint8_t, (((uint8_t *)&t744) + 0));
                    if (t745 != ((uint8_t)11ULL)) goto ie740;
                    gm_t88rcucg1b7uk t746 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t744) + 8) + 8));
                    gm_l60 = t746;
                    {
                        void * t748 = gm_l0;
                        gm_t88rcucg1b7uk t749 = gm_l59;
                        uint64_t t750 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t748, t749);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t751 = gm_f_us_cuv01fp(t750);
                        gm_l61 = t751;
                    }
                    {
                        void * t752 = gm_l0;
                        gm_t88rcucg1b7uk t753 = gm_l60;
                        uint64_t t754 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t752, t753);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t755 = gm_f_us_cuv01fp(t754);
                        gm_l62 = t755;
                    }
                    {
                        gm_tvdg94bk7aa22 t756;
                        void * t758 = gm_l0;
                        gm_t88rcucg1b7uk t759 = gm_l60;
                        gm_t53at80pfdus3 t760;
                        gm_f_kind_htv6093((void *)&t760, t758, GM_CONV(gm_csguankko4lmhd, t759));
                        gm_t53at80pfdus3 t761 = t760;
                        uint8_t t762;
                        uint8_t t765 = GM_LD(uint8_t, (((uint8_t *)&t761) + 0));
                        if (t765 != ((uint8_t)3ULL)) goto mn764;
                        t762 = ((uint8_t)1);
                        goto me763;
mn764:;
                        t762 = ((uint8_t)0);
                        goto me763;
mn766:;
                        gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 100, 35);
me763:;
                        if (!t762) goto ie757;
                        gm_tvdg94bk7aa22 t768;
                        GM_ST(uint8_t, (uint8_t *)&t768 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t768 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t768 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t768 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t768 + 0), ((uint8_t)2ULL));
                        gm_tvdg94bk7aa22 t769 = t768;
                        t756 = t769;
                        goto ix757;
ie757:;
                        gm_tvdg94bk7aa22 t771;
                        GM_ST(uint8_t, (uint8_t *)&t771 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t771 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t771 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t771 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t771 + 0), ((uint8_t)0ULL));
                        gm_tvdg94bk7aa22 t772 = t771;
                        t756 = t772;
ix757:;
                        gm_l63 = t756;
                    }
                    {
                        {
                            gm_l65 = ((uint64_t)0ULL);
                        }
                        {
                            uint64_t t774 = gm_l58;
                            uint64_t t775 = gm_f_us_cuv01fp(t774);
                            gm_l64 = t775;
                        }
                        {
                            for (;;) {
                                uint64_t t777 = gm_l65;
                                uint64_t t778 = gm_l64;
                                uint8_t t779 = (t777 < t778);
                                if (!t779) goto lb776;
                                {
                                    uint64_t t782 = gm_l65;
                                    uint64_t t783 = t782;
                                    uint64_t t786 = GM_LD(uint64_t, ((uint8_t *)&t783));
                                    gm_l66 = t786;
                                    {
                                        void * t788 = gm_l0;
                                        gm_tl874t2atnlh3 t789 = gm_l1;
                                        gm_tvdg94bk7aa22 t790 = gm_l63;
                                        gm_t88rcucg1b7uk t791 = gm_l59;
                                        gm_t88rcucg1b7uk t792 = gm_l60;
                                        uint64_t t793 = (uintptr_t)((uint64_t)0ULL);
                                        uint64_t t794 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                        if (t793 >= (uintptr_t)(t794)) gm_trap_index(t793, t794, gm_srcns60k96jj1mt, 101, 68);
                                        uint64_t t795 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t793 * 16) + 8);
                                        void * t796 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t793 * 16)));
                                        uint64_t t797 = gm_l66;
                                        uint64_t t798 = gm_l61;
                                        uint64_t r799; bool o800 = __builtin_mul_overflow(t797, t798, &r799);
                                        if (o800) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 101, 73);
                                        uint64_t t801 = r799;
                                        uint64_t t802 = (uintptr_t)t801;
                                        uint64_t t803 = gm_l66;
                                        uint64_t r804; bool o805 = __builtin_add_overflow(t803, ((uint64_t)1ULL), &r804);
                                        if (o805) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 101, 83);
                                        uint64_t t806 = r804;
                                        uint64_t t807 = gm_l61;
                                        uint64_t r808; bool o809 = __builtin_mul_overflow(t806, t807, &r808);
                                        if (o809) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 101, 82);
                                        uint64_t t810 = r808;
                                        uint64_t t811 = (uintptr_t)t810;
                                        if (t802 > t811 || t811 > (uintptr_t)t795) gm_trap_slice(t802, t811, t795, gm_srcns60k96jj1mt, 101, 68);
                                        gm_b16_8 t812;
                                        GM_ST(void *, (uint8_t *)&t812, (void *)(((uint8_t *)t796 + t802 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t812 + 8, (uintptr_t)(t811 - t802));
                                        gm_b16_8 t813 = t812;
                                        gm_b16_8 t814 = GM_LD(gm_b16_8, ((uint8_t *)&t813));
                                        uint64_t t815 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                        void * t816 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                                        uint64_t t817 = gm_l66;
                                        uint64_t t818 = gm_l62;
                                        uint64_t r819; bool o820 = __builtin_mul_overflow(t817, t818, &r819);
                                        if (o820) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 101, 102);
                                        uint64_t t821 = r819;
                                        uint64_t t822 = (uintptr_t)t821;
                                        uint64_t t823 = gm_l66;
                                        uint64_t r824; bool o825 = __builtin_add_overflow(t823, ((uint64_t)1ULL), &r824);
                                        if (o825) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 101, 112);
                                        uint64_t t826 = r824;
                                        uint64_t t827 = gm_l62;
                                        uint64_t r828; bool o829 = __builtin_mul_overflow(t826, t827, &r828);
                                        if (o829) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 101, 111);
                                        uint64_t t830 = r828;
                                        uint64_t t831 = (uintptr_t)t830;
                                        if (t822 > t831 || t831 > (uintptr_t)t815) gm_trap_slice(t822, t831, t815, gm_srcns60k96jj1mt, 101, 98);
                                        gm_b16_8 t832;
                                        GM_ST(void *, (uint8_t *)&t832, (void *)(((uint8_t *)t816 + t822 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t832 + 8, (uintptr_t)(t831 - t822));
                                        gm_tcfa6l7reoosn t833;
                                        gm_f_convert_is6an2k((void *)&t833, (void *)NULL, cx, NULL, t788, t789, t790, t791, t792, t814, t832);
                                        if (gm_unw.kind) {
                                            goto gm_ll780;
                                        }
                                        gm_tcfa6l7reoosn t834 = t833;
                                        if (((((uint8_t *)&t834))[0] != 0)) {
                                            gm_t76123gfet73g t835 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t834) + 8));
                                            gm_tcfa6l7reoosn t836;
                                            GM_ST(uint8_t, (uint8_t *)&t836 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t836 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t836 + 4, 0);
                                            ((uint8_t *)&t836)[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t836 + 8), t835);
                                            F.ret = t836;
                                            goto gm_fret;
                                        }
                                        gm_b0_1 t837 = GM_LD(gm_b0_1, (((uint8_t *)&t834) + 8));
                                    }
                                    goto me784;
mn785:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 101, 13);
me784:;
                                }
lc776:;
                                {
                                    uint64_t t839 = gm_l65;
                                    uint64_t r840; bool o841 = __builtin_add_overflow(t839, ((uint64_t)1ULL), &r840);
                                    uint64_t t842 = r840;
                                    gm_l65 = t842;
                                }
                            }
gm_ll780:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 8) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb776; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc776; }
                            }
                            goto gm_lad;
lb776:;
                        }
                    }
                    goto ix740;
ie740:;
                    {
                        void * t844 = gm_l0;
                        gm_tl874t2atnlh3 t845 = gm_l1;
                        gm_b16_8 t846;
                        GM_ST(void *, (uint8_t *)&t846, (void *)(gm_smsrao7h22muv));
                        GM_ST(uintptr_t, (uint8_t *)&t846 + 8, (uintptr_t)(60));
                        gm_t76123gfet73g t847;
                        gm_f_trap_qh84l9d((void *)&t847, (void *)NULL, cx, NULL, t844, t845, t846);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tcfa6l7reoosn t848;
                        GM_ST(uint8_t, (uint8_t *)&t848 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t848 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t848 + 4, 0);
                        ((uint8_t *)&t848)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t848 + 8), t847);
                        gm_tcfa6l7reoosn t849 = t848;
                        uint8_t t850 = ((((uint8_t *)&t849))[0] != 0);
                        F.ret = t848;
                        goto gm_fret;
                    }
ix740:;
                }
                goto ix731;
ie731:;
                {
                    void * t852 = gm_l0;
                    gm_tl874t2atnlh3 t853 = gm_l1;
                    gm_b16_8 t854;
                    GM_ST(void *, (uint8_t *)&t854, (void *)(gm_spnr4ftjbm9m9));
                    GM_ST(uintptr_t, (uint8_t *)&t854 + 8, (uintptr_t)(60));
                    gm_t76123gfet73g t855;
                    gm_f_trap_qh84l9d((void *)&t855, (void *)NULL, cx, NULL, t852, t853, t854);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t856;
                    GM_ST(uint8_t, (uint8_t *)&t856 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t856 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t856 + 4, 0);
                    ((uint8_t *)&t856)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t856 + 8), t855);
                    gm_tcfa6l7reoosn t857 = t856;
                    uint8_t t858 = ((((uint8_t *)&t857))[0] != 0);
                    F.ret = t856;
                    goto gm_fret;
                }
ix731:;
            }
            goto me3;
mn728:;
ma12:;
            uint8_t t860 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t860 != ((uint8_t)57ULL)) goto mn859;
            {
                void * t863 = gm_l0;
                gm_t88rcucg1b7uk t864 = gm_l4;
                gm_t53at80pfdus3 t865;
                gm_f_kind_htv6093((void *)&t865, t863, GM_CONV(gm_csguankko4lmhd, t864));
                gm_t53at80pfdus3 t866 = t865;
                uint8_t t867 = GM_LD(uint8_t, (((uint8_t *)&t866) + 0));
                if (t867 != ((uint8_t)11ULL)) goto ie862;
                uint64_t t868 = GM_LD(uint64_t, ((((uint8_t *)&t866) + 8) + 0));
                gm_l67 = t868;
                gm_t88rcucg1b7uk t869 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t866) + 8) + 8));
                gm_l68 = t869;
                {
                    void * t871 = gm_l0;
                    gm_t88rcucg1b7uk t872 = gm_l68;
                    uint64_t t873 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t871, t872);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t874 = gm_f_us_cuv01fp(t873);
                    gm_l69 = t874;
                }
                {
                    {
                        gm_l71 = ((uint64_t)1ULL);
                    }
                    {
                        uint64_t t876 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_l70 = t876;
                    }
                    {
                        for (;;) {
                            uint64_t t878 = gm_l71;
                            uint64_t t879 = gm_l70;
                            uint8_t t880 = (t878 < t879);
                            if (!t880) goto lb877;
                            {
                                uint64_t t883 = gm_l71;
                                uint64_t t884 = t883;
                                uint64_t t887 = GM_LD(uint64_t, ((uint8_t *)&t884));
                                gm_l72 = t887;
                                {
                                    void * t889 = gm_l0;
                                    uint64_t t890 = gm_l72;
                                    uint64_t t891 = (uintptr_t)t890;
                                    uint64_t t892 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    if (t891 >= (uintptr_t)(t892)) gm_trap_index(t891, t892, gm_srcns60k96jj1mt, 107, 35);
                                    gm_b16_8 t893 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t891 * 16));
                                    gm_b16_8 t894 = t893;
                                    gm_b16_8 t895 = GM_LD(gm_b16_8, ((uint8_t *)&t894));
                                    uint64_t t896 = gm_f_get_u64_tgpb2vh(t889, t895);
                                    uint64_t t897 = gm_f_us_cuv01fp(t896);
                                    gm_l73 = t897;
                                }
                                {
                                    uint64_t t899 = gm_l73;
                                    uint64_t t900 = gm_l67;
                                    uint64_t t901 = gm_f_us_cuv01fp(t900);
                                    uint8_t t902 = (t899 >= t901);
                                    if (!t902) goto ie898;
                                    {
                                        void * t904 = gm_l0;
                                        gm_tl874t2atnlh3 t905 = gm_l1;
                                        gm_b16_8 t906;
                                        GM_ST(void *, (uint8_t *)&t906, (void *)(gm_ssvci9220kuoj));
                                        GM_ST(uintptr_t, (uint8_t *)&t906 + 8, (uintptr_t)(44));
                                        gm_t76123gfet73g t907;
                                        gm_f_trap_qh84l9d((void *)&t907, (void *)NULL, cx, NULL, t904, t905, t906);
                                        if (gm_unw.kind) {
                                            goto gm_ll881;
                                        }
                                        gm_tcfa6l7reoosn t908;
                                        GM_ST(uint8_t, (uint8_t *)&t908 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t908 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t908 + 4, 0);
                                        ((uint8_t *)&t908)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t908 + 8), t907);
                                        gm_tcfa6l7reoosn t909 = t908;
                                        uint8_t t910 = ((((uint8_t *)&t909))[0] != 0);
                                        F.ret = t908;
                                        goto gm_fret;
                                    }
                                    goto ix898;
ie898:;
ix898:;
                                }
                                {
                                    uint64_t t911 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    void * t912 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                                    uint64_t t913 = gm_l72;
                                    uint64_t r914; bool o915 = __builtin_sub_overflow(t913, ((uint64_t)1ULL), &r914);
                                    if (o915) gm_trap("integer overflow in `-`", gm_srcns60k96jj1mt, 109, 22);
                                    uint64_t t916 = r914;
                                    uint64_t t917 = gm_l69;
                                    uint64_t r918; bool o919 = __builtin_mul_overflow(t916, t917, &r918);
                                    if (o919) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 109, 21);
                                    uint64_t t920 = r918;
                                    uint64_t t921 = (uintptr_t)t920;
                                    uint64_t t922 = gm_l72;
                                    uint64_t t923 = gm_l69;
                                    uint64_t r924; bool o925 = __builtin_mul_overflow(t922, t923, &r924);
                                    if (o925) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 109, 35);
                                    uint64_t t926 = r924;
                                    uint64_t t927 = (uintptr_t)t926;
                                    if (t921 > t927 || t927 > (uintptr_t)t911) gm_trap_slice(t921, t927, t911, gm_srcns60k96jj1mt, 109, 17);
                                    gm_b16_8 t928;
                                    GM_ST(void *, (uint8_t *)&t928, (void *)(((uint8_t *)t912 + t921 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t928 + 8, (uintptr_t)(t927 - t921));
                                    uint64_t t929 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t930 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    if (t929 >= (uintptr_t)(t930)) gm_trap_index(t929, t930, gm_srcns60k96jj1mt, 109, 53);
                                    uint64_t t931 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t929 * 16) + 8);
                                    void * t932 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t929 * 16)));
                                    uint64_t t933 = gm_l73;
                                    uint64_t t934 = gm_l69;
                                    uint64_t r935; bool o936 = __builtin_mul_overflow(t933, t934, &r935);
                                    if (o936) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 109, 58);
                                    uint64_t t937 = r935;
                                    uint64_t t938 = (uintptr_t)t937;
                                    uint64_t t939 = gm_l73;
                                    uint64_t r940; bool o941 = __builtin_add_overflow(t939, ((uint64_t)1ULL), &r940);
                                    if (o941) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 109, 67);
                                    uint64_t t942 = r940;
                                    uint64_t t943 = gm_l69;
                                    uint64_t r944; bool o945 = __builtin_mul_overflow(t942, t943, &r944);
                                    if (o945) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 109, 66);
                                    uint64_t t946 = r944;
                                    uint64_t t947 = (uintptr_t)t946;
                                    if (t938 > t947 || t947 > (uintptr_t)t931) gm_trap_slice(t938, t947, t931, gm_srcns60k96jj1mt, 109, 53);
                                    gm_b16_8 t948;
                                    GM_ST(void *, (uint8_t *)&t948, (void *)(((uint8_t *)t932 + t938 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t948 + 8, (uintptr_t)(t947 - t938));
                                    gm_b16_8 t949 = t948;
                                    gm_b16_8 t950 = GM_LD(gm_b16_8, ((uint8_t *)&t949));
                                    gm_f_copy_from_f7j6p05(t928, t950);
                                }
                                goto me885;
mn886:;
                                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 106, 13);
me885:;
                            }
lc877:;
                            {
                                uint64_t t952 = gm_l71;
                                uint64_t r953; bool o954 = __builtin_add_overflow(t952, ((uint64_t)1ULL), &r953);
                                uint64_t t955 = r953;
                                gm_l71 = t955;
                            }
                        }
gm_ll881:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 9) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb877; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc877; }
                        }
                        goto gm_lad;
lb877:;
                    }
                }
                goto ix862;
ie862:;
                {
                    void * t957 = gm_l0;
                    gm_tl874t2atnlh3 t958 = gm_l1;
                    gm_b16_8 t959;
                    GM_ST(void *, (uint8_t *)&t959, (void *)(gm_s1tv0n9hk7497));
                    GM_ST(uintptr_t, (uint8_t *)&t959 + 8, (uintptr_t)(57));
                    gm_t76123gfet73g t960;
                    gm_f_trap_qh84l9d((void *)&t960, (void *)NULL, cx, NULL, t957, t958, t959);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t961;
                    GM_ST(uint8_t, (uint8_t *)&t961 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t961 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t961 + 4, 0);
                    ((uint8_t *)&t961)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t961 + 8), t960);
                    gm_tcfa6l7reoosn t962 = t961;
                    uint8_t t963 = ((((uint8_t *)&t962))[0] != 0);
                    F.ret = t961;
                    goto gm_fret;
                }
ix862:;
            }
            goto me3;
mn859:;
ma13:;
            void * t965 = gm_l0;
            gm_tl874t2atnlh3 t966 = gm_l1;
            gm_b16_8 t967;
            GM_ST(void *, (uint8_t *)&t967, (void *)(gm_s5e5797b1jurs));
            GM_ST(uintptr_t, (uint8_t *)&t967 + 8, (uintptr_t)(19));
            gm_t76123gfet73g t968;
            gm_f_unsupported_70vvk1e((void *)&t968, (void *)NULL, cx, NULL, t965, t966, t967);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t969;
            GM_ST(uint8_t, (uint8_t *)&t969 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t969 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t969 + 4, 0);
            ((uint8_t *)&t969)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t969 + 8), t968);
            gm_tcfa6l7reoosn t970 = t969;
            uint8_t t971 = ((((uint8_t *)&t970))[0] != 0);
            F.ret = t969;
            goto gm_fret;
            goto me3;
mn964:;
ma14:;
            gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 7, 5);
me3:;
        }
    }
    gm_b0_1 t972;
    gm_tcfa6l7reoosn t973;
    GM_ST(uint8_t, (uint8_t *)&t973 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t973 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t973 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t973 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t973 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t973 + 24, 0);
    ((uint8_t *)&t973)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t973 + 8), t972);
    gm_tcfa6l7reoosn t974 = t973;
    F.ret = t974;
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
