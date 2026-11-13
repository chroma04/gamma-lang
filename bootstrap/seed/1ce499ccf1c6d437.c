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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; uint64_t v1; uint64_t v2; } f1; } gm_tu0a56t4bfvqh;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t p3[6]; gm_b16_8 f3; void * f4; void * f5; gm_tl874t2atnlh3 f6; uint8_t pe[4]; } gm_t24301fas1b2a;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_tl874t2atnlh3 f3; gm_tl874t2atnlh3 f4; gm_b16_8 f5; } gm_t80lhgi01aeei;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_tn68el8d14vku v1; } f1; } gm_tsncjtib3j1l1;
_Static_assert(sizeof(gm_tu0a56t4bfvqh) == 16 && _Alignof(gm_tu0a56t4bfvqh) == 8 && offsetof(gm_tu0a56t4bfvqh, f0) == 0 && offsetof(gm_tu0a56t4bfvqh, f1) == 8, "layout of `Base`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t24301fas1b2a) == 72 && _Alignof(gm_t24301fas1b2a) == 8 && offsetof(gm_t24301fas1b2a, f0) == 0 && offsetof(gm_t24301fas1b2a, f1) == 16 && offsetof(gm_t24301fas1b2a, f2) == 17 && offsetof(gm_t24301fas1b2a, f3) == 24 && offsetof(gm_t24301fas1b2a, f4) == 40 && offsetof(gm_t24301fas1b2a, f5) == 48 && offsetof(gm_t24301fas1b2a, f6) == 56, "layout of `FieldDecl`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; } gm_t6ok5tvcjc200;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t80lhgi01aeei) == 80 && _Alignof(gm_t80lhgi01aeei) == 8 && offsetof(gm_t80lhgi01aeei, f0) == 0 && offsetof(gm_t80lhgi01aeei, f1) == 16 && offsetof(gm_t80lhgi01aeei, f2) == 24 && offsetof(gm_t80lhgi01aeei, f3) == 40 && offsetof(gm_t80lhgi01aeei, f4) == 52 && offsetof(gm_t80lhgi01aeei, f5) == 64, "layout of `DocSymbol`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_th2lqbo77pvsc f3; } gm_tl5cuhjfnc4cr;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
typedef struct { gm_t08olq97rscn3 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tnljbma3s3h16;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsncjtib3j1l1) == 24 && _Alignof(gm_tsncjtib3j1l1) == 8 && offsetof(gm_tsncjtib3j1l1, f0) == 0 && offsetof(gm_tsncjtib3j1l1, f1) == 8, "layout of `DeclRef`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t6ok5tvcjc200) == 40 && _Alignof(gm_t6ok5tvcjc200) == 8 && offsetof(gm_t6ok5tvcjc200, f0) == 0 && offsetof(gm_t6ok5tvcjc200, f1) == 16, "layout of `Key`");
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
_Static_assert(sizeof(gm_tl5cuhjfnc4cr) == 64 && _Alignof(gm_tl5cuhjfnc4cr) == 8 && offsetof(gm_tl5cuhjfnc4cr, f0) == 0 && offsetof(gm_tl5cuhjfnc4cr, f1) == 16 && offsetof(gm_tl5cuhjfnc4cr, f2) == 24 && offsetof(gm_tl5cuhjfnc4cr, f3) == 40, "layout of `Item`");
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tnljbma3s3h16) == 64 && _Alignof(gm_tnljbma3s3h16) == 8 && offsetof(gm_tnljbma3s3h16, f0) == 0 && offsetof(gm_tnljbma3s3h16, f1) == 32 && offsetof(gm_tnljbma3s3h16, f2) == 40 && offsetof(gm_tnljbma3s3h16, f3) == 48, "layout of `Variant`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { gm_t6qooom8qa2do f0; uint8_t f1; uint8_t pe[7]; } gm_tlabdfckddbfd;
_Static_assert(sizeof(gm_tlabdfckddbfd) == 216 && _Alignof(gm_tlabdfckddbfd) == 8 && offsetof(gm_tlabdfckddbfd, f0) == 0 && offsetof(gm_tlabdfckddbfd, f1) == 208, "layout of `?DeclValue`");
struct gm_fr_clauses_posk098 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_first_file_of_umgolc2 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_fn_literal_sodo3l0 {
    gm_frame h;
    gm_t79rbdv4rafe5 l5; /* b */
    gm_b0_1 ret;
};
struct gm_fr_handler_literal_bbdui98 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_item_4kot0vg {
    gm_frame h;
    gm_tv2uf4leil5hq l3; /* b */
    gm_t7bjbmukv26e7 l4; /* x */
    gm_tcfiotghd1s5k l23; /* t */
    gm_b0_1 ret;
};
struct gm_fr_params_ufsghpd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sig_89f514c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sig_rest_gkcl8d2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_struct_members_0n9oe2e {
    gm_frame h;
    gm_t24301fas1b2a l6; /* f */
    gm_t7bjbmukv26e7 l13; /* x */
    gm_b0_1 ret;
};
struct gm_fr_top_binding_pp9dk39 {
    gm_frame h;
    gm_tl5cuhjfnc4cr l11; /* itm */
    gm_trqfvmjtt4sij l12; /* children */
    gm_t6qooom8qa2do l22; /* v */
    gm_tlivgedjs5ps3 l31; /* sg */
    gm_b0_1 ret;
};
struct gm_fr_type_lit_lm0h7k6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_type_literal_nnom1cv {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b0_1 ret;
};
struct gm_fr_union_members_sg9s47f {
    gm_frame h;
    gm_tnljbma3s3h16 l6; /* v */
    gm_t7bjbmukv26e7 l12; /* x */
    gm_b0_1 ret;
};
bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b);
bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_ref_tgbe5vc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_jfodv9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tu0a56t4bfvqh gm_f_classify_pdaj2gb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_clauses_posk098(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) void gm_f_decl_done_mk70ol9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_decl_item_a57s8if(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_feg3ue0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, uint32_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_def_once_pqgvduh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6ld1tun(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_ensure_hover_3epdjmk(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_expr_kih3eo8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_first_file_of_umgolc2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_literal_sodo3l0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_407qqln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jf46ta8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_handler_literal_bbdui98(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_item_4kot0vg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_list_uc1atd0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_operation_use_ue1or58(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_params_ufsghpd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_pop_scope_vgka5ae(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_le0hqcb(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_u4jfguj(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_sig_89f514c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_sig_rest_gkcl8d2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_fjcc76p(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_struct_members_0n9oe2e(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_top_binding_pp9dk39(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_type_lit_lm0h7k6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_type_literal_nnom1cv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_union_members_sg9s47f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s9r64t1166q1f[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src94heo671ss5e[];
extern const uint8_t gm_srfagufvviad7[6];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) gm_b0_1 gm_f_clauses_posk098(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2) {
    struct gm_fr_clauses_posk098 F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* cs */
    gm_tf5srh4mp4b3i gm_l2; /* effect_ */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    void * gm_l6; /* c */
    uint64_t gm_l7; /* d */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t08olq97rscn3 gm_l10; /* p */
    GM_STACK_CHECK(&F, 443u);
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
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l3 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = gm_l3;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = t10;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_src94heo671ss5e, 74, 15);
                        void * t19 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t17 * 72);
                        gm_l6 = t19;
                    }
                    {
                        if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie20;
                        uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                        gm_l7 = t21;
                        {
                            void * t23 = gm_l0;
                            uint64_t t24 = gm_l7;
                            void * t25 = gm_l6;
                            gm_b0_1 t26 = gm_f_operation_use_ue1or58((void *)NULL, cx, NULL, t23, t24, &(*(gm_t08olq97rscn3 *)(((uint8_t *)t25) + 0)));
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto ix20;
ie20:;
ix20:;
                    }
                    {
                        void * t27 = gm_l0;
                        void * t28 = gm_l6;
                        uint32_t t29 = GM_LD(uint32_t, ((((uint8_t *)t28) + 56) + 8));
                        gm_b0_1 t30 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t27, t29);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        {
                            void * t32 = gm_l6;
                            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 32));
                            gm_l8 = t33;
                        }
                        {
                            gm_l9 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t35 = gm_l9;
                                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                uint8_t t37 = (t35 < t36);
                                if (!t37) goto lb34;
                                {
                                    uint64_t t40 = gm_l9;
                                    uint64_t t41 = (uintptr_t)t40;
                                    uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                    gm_t08olq97rscn3 t45 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t41 * 32));
                                    gm_l10 = t45;
                                    {
                                        void * t47 = gm_l0;
                                        gm_t9qkh0rsooc8d t48;
                                        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)9ULL));
                                        uint32_t t49 = GM_LD(uint32_t, ((((uint8_t *)&gm_l10) + 16) + 8));
                                        gm_b0_1 t50 = gm_f_declare_feg3ue0((void *)NULL, cx, NULL, t47, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l10)), t48, t49);
                                        if (gm_unw.kind) {
                                            goto gm_ll38;
                                        }
                                    }
                                    goto me43;
mn44:;
                                    gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 77, 9);
me43:;
                                }
lc34:;
                                {
                                    uint64_t t52 = gm_l9;
                                    uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                                    uint64_t t55 = r53;
                                    gm_l9 = t55;
                                }
                            }
gm_ll38:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb34; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc34; }
                            }
                            goto gm_ll8;
lb34:;
                        }
                    }
                    {
                        void * t56 = gm_l0;
                        void * t57 = gm_l6;
                        void * t58 = GM_LD(void *, (((uint8_t *)t57) + 48));
                        gm_b0_1 t59 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t56, t58);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        void * t60 = gm_l0;
                        gm_f_pop_scope_vgka5ae(t60);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 73, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t62 = gm_l4;
                    uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                    uint64_t t65 = r63;
                    gm_l4 = t65;
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_first_file_of_umgolc2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_first_file_of_umgolc2 F;
    void * gm_l0; /* w */
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l2; /* dir */
    void * gm_l3; /* sm */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 440u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 200) + 0) + 8);
        if (t3 >= (uintptr_t)(t7)) gm_trap_index(t3, t7, gm_src94heo671ss5e, 331, 12);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 200) + 0)) + t3 * 184) + 120));
        gm_l2 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        gm_l3 = t12;
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            void * t14 = gm_l3;
            uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 0) + 0) + 8);
            gm_l4 = t15;
        }
        {
            for (;;) {
                uint64_t t17 = gm_l5;
                uint64_t t18 = gm_l4;
                uint8_t t19 = (t17 < t18);
                if (!t19) goto lb16;
                {
                    uint64_t t22 = gm_l5;
                    uint64_t t23 = t22;
                    uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)&t23));
                    gm_l6 = t26;
                    {
                        uint64_t t29 = gm_l6;
                        uint64_t t30 = (uintptr_t)t29;
                        void * t31 = gm_l3;
                        uint64_t t32 = GM_LD(uintptr_t, ((((uint8_t *)t31) + 0) + 0) + 8);
                        if (t30 >= (uintptr_t)(t32)) gm_trap_index(t30, t32, gm_src94heo671ss5e, 334, 24);
                        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t31) + 0) + 0)) + t30 * 80) + 0));
                        gm_th2lqbo77pvsc t34;
                        gm_f_parent_lk0iero((void *)&t34, (void *)NULL, cx, NULL, t33);
                        if (gm_unw.kind) {
                            goto gm_ll20;
                        }
                        gm_b16_8 t35 = gm_l2;
                        gm_th2lqbo77pvsc t36;
                        GM_ST(uint8_t, (uint8_t *)&t36 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t36 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t36 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t36), t35);
                        ((uint8_t *)&t36)[16] = 1;
                        gm_th2lqbo77pvsc t37 = t34;
                        gm_th2lqbo77pvsc t38 = t36;
                        uint8_t t39 = (gm_eq0ogca82g1f(((uint8_t *)&t37), ((uint8_t *)&t38)));
                        if (!t39) goto ie28;
                        {
                            uint64_t t41 = gm_l6;
                            uint32_t t42 = (uint32_t)t41;
                            gm_t9805qtonbsag t43;
                            GM_ST(uint8_t, (uint8_t *)&t43 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t43 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t43), t42);
                            ((uint8_t *)&t43)[4] = 1;
                            uint8_t t44 = 0;
                            F.ret = t43;
                            goto gm_fret;
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 333, 5);
me24:;
                }
lc16:;
                {
                    uint64_t t46 = gm_l5;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l5 = t49;
                }
            }
gm_ll20:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb16; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc16; }
            }
            goto gm_lad;
lb16:;
        }
    }
    {
        gm_t9805qtonbsag t50;
        memset(&t50, 0, sizeof t50);
        uint8_t t51 = 0;
        F.ret = t50;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_literal_sodo3l0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_literal_sodo3l0 F;
    void * gm_l0; /* w */
    void * gm_l1; /* lit */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l6; /* e */
    GM_STACK_CHECK(&F, 375u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        uint32_t t4 = GM_LD(uint32_t, ((((uint8_t *)t3) + 136) + 8));
        gm_b0_1 t5 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t2, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l1;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)t7) + 0) + 0));
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_b0_1 t11 = gm_f_params_ufsghpd((void *)NULL, cx, NULL, t6, t10, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l1;
        void * t14 = (void *)(((uint8_t *)t13) + 0);
        gm_b0_1 t15 = gm_f_sig_rest_gkcl8d2((void *)NULL, cx, NULL, t12, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t17 = gm_l1;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 72) + 8);
            gm_l2 = t18;
        }
        {
            for (;;) {
                uint64_t t20 = gm_l3;
                uint64_t t21 = gm_l2;
                uint8_t t22 = (t20 < t21);
                if (!t22) goto lb19;
                {
                    uint64_t t25 = gm_l3;
                    uint64_t t26 = t25;
                    uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&t26));
                    gm_l4 = t29;
                    {
                        void * t31 = gm_l0;
                        uint64_t t32 = gm_l4;
                        uint64_t t33 = (uintptr_t)t32;
                        void * t34 = gm_l1;
                        uint64_t t35 = GM_LD(uintptr_t, (((uint8_t *)t34) + 72) + 8);
                        if (t33 >= (uintptr_t)(t35)) gm_trap_index(t33, t35, gm_src94heo671ss5e, 15, 41);
                        void * t36 = GM_LD(void *, (((uint8_t *)GM_LD(void *, (((uint8_t *)t34) + 72)) + t33 * 56) + 32));
                        gm_b0_1 t37 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t31, t36);
                        if (gm_unw.kind) {
                            goto gm_ll23;
                        }
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 15, 5);
me27:;
                }
lc19:;
                {
                    uint64_t t39 = gm_l3;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    uint64_t t42 = r40;
                    gm_l3 = t42;
                }
            }
gm_ll23:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
            }
            goto gm_lad;
lb19:;
        }
    }
    {
        void * t43 = gm_l1;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t43) + 88) + 0));
        if (t46 != ((uint8_t)0ULL)) goto mn45;
        gm_t79rbdv4rafe5 t47 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t43) + 88) + 8));
        F.l5 = t47;
        void * t48 = gm_l0;
        void * t49 = (void *)((uint8_t *)&F.l5);
        gm_b0_1 t50 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me44;
mn45:;
        uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t43) + 88) + 0));
        if (t52 != ((uint8_t)1ULL)) goto mn51;
        void * t53 = GM_LD(void *, ((((uint8_t *)t43) + 88) + 8));
        gm_l6 = t53;
        void * t54 = gm_l0;
        void * t55 = gm_l6;
        gm_b0_1 t56 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me44;
mn51:;
        gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 16, 5);
me44:;
    }
    {
        void * t57 = gm_l0;
        gm_f_pop_scope_vgka5ae(t57);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_handler_literal_bbdui98(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_handler_literal_bbdui98 F;
    void * gm_l0; /* w */
    void * gm_l1; /* h */
    gm_tf5srh4mp4b3i gm_l2; /* effect_ */
    uint64_t gm_l3; /* d */
    GM_STACK_CHECK(&F, 249u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        gm_b0_1 t5 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t2, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tf5srh4mp4b3i t6;
        memset(&t6, 0, sizeof t6);
        gm_l2 = t6;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l1;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        gm_tu0a56t4bfvqh t11 = gm_f_classify_pdaj2gb((void *)NULL, cx, NULL, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tu0a56t4bfvqh t12 = t11;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t13 != ((uint8_t)2ULL)) goto ie7;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&t12) + 8));
        gm_l3 = t14;
        {
            uint64_t t16 = gm_l3;
            gm_tf5srh4mp4b3i t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t17), t16);
            ((uint8_t *)&t17)[8] = 1;
            gm_l2 = t17;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l1;
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t19) + 8));
        gm_b16_8 t21 = t20;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_tf5srh4mp4b3i t23 = gm_l2;
        gm_b0_1 t24 = gm_f_clauses_posk098((void *)NULL, cx, NULL, t18, t22, t23);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_item_4kot0vg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_item_4kot0vg F;
    void * gm_l0; /* w */
    void * gm_l1; /* it */
    void * gm_l2; /* out */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    gm_b16_8 gm_l8; /* e */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    void * gm_l12; /* h */
    gm_b16_8 gm_l13; /* ps */
    gm_b16_8 gm_l14; /* effects */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    gm_tf5srh4mp4b3i gm_l18; /* effect_ */
    uint64_t gm_l19; /* d */
    gm_b16_8 gm_l20; /* text */
    gm_b16_8 gm_l21; /* head */
    uint64_t gm_l22; /* i */
    gm_b16_8 gm_l24; /* word */
    void * gm_l25; /* e */
    GM_STACK_CHECK(&F, 2061u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 64) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; default: goto ma9; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 64) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        gm_tv2uf4leil5hq t13 = GM_LD(gm_tv2uf4leil5hq, ((((uint8_t *)t2) + 64) + 8));
        F.l3 = t13;
        void * t14 = gm_l0;
        void * t15 = gm_l1;
        void * t16 = (void *)((uint8_t *)&F.l3);
        void * t17 = gm_l2;
        gm_b0_1 t18 = gm_f_top_binding_pp9dk39((void *)NULL, cx, NULL, t14, t15, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn11:;
ma5:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t2) + 64) + 0));
        if (t20 != ((uint8_t)1ULL)) goto mn19;
        gm_t7bjbmukv26e7 t21 = GM_LD(gm_t7bjbmukv26e7, ((((uint8_t *)t2) + 64) + 8));
        F.l4 = t21;
        {
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, (((uint8_t *)&F.l4) + 0));
            gm_b0_1 t25 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                uint64_t t27 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 8) + 8);
                gm_l5 = t27;
            }
            {
                for (;;) {
                    uint64_t t29 = gm_l6;
                    uint64_t t30 = gm_l5;
                    uint8_t t31 = (t29 < t30);
                    if (!t31) goto lb28;
                    {
                        uint64_t t34 = gm_l6;
                        uint64_t t35 = t34;
                        uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&t35));
                        gm_l7 = t38;
                        {
                            void * t40 = gm_l0;
                            uint64_t t41 = gm_l7;
                            uint64_t t42 = (uintptr_t)t41;
                            uint64_t t43 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 8) + 8);
                            if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_src94heo671ss5e, 198, 46);
                            void * t44 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 8)) + t42 * 160);
                            void * t45 = gm_l2;
                            gm_b0_1 t46 = gm_f_item_4kot0vg((void *)NULL, cx, NULL, t40, t44, t45);
                            if (gm_unw.kind) {
                                goto gm_ll32;
                            }
                        }
                        goto me36;
mn37:;
                        gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 198, 13);
me36:;
                    }
lc28:;
                    {
                        uint64_t t48 = gm_l6;
                        uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                        uint64_t t51 = r49;
                        gm_l6 = t51;
                    }
                }
gm_ll32:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb28; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc28; }
                }
                goto gm_lad;
lb28:;
            }
        }
        {
            if (!(((((uint8_t *)&F.l4) + 24))[16] != 0)) goto ie52;
            gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 24));
            gm_l8 = t53;
            {
                {
                    gm_l10 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_l9 = t56;
                }
                {
                    for (;;) {
                        uint64_t t58 = gm_l10;
                        uint64_t t59 = gm_l9;
                        uint8_t t60 = (t58 < t59);
                        if (!t60) goto lb57;
                        {
                            uint64_t t63 = gm_l10;
                            uint64_t t64 = t63;
                            uint64_t t67 = GM_LD(uint64_t, ((uint8_t *)&t64));
                            gm_l11 = t67;
                            {
                                void * t69 = gm_l0;
                                uint64_t t70 = gm_l11;
                                uint64_t t71 = (uintptr_t)t70;
                                uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                if (t71 >= (uintptr_t)(t72)) gm_trap_index(t71, t72, gm_src94heo671ss5e, 200, 45);
                                void * t73 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t71 * 160);
                                void * t74 = gm_l2;
                                gm_b0_1 t75 = gm_f_item_4kot0vg((void *)NULL, cx, NULL, t69, t73, t74);
                                if (gm_unw.kind) {
                                    goto gm_ll61;
                                }
                            }
                            goto me65;
mn66:;
                            gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 200, 17);
me65:;
                        }
lc57:;
                        {
                            uint64_t t77 = gm_l10;
                            uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                            uint64_t t80 = r78;
                            gm_l10 = t80;
                        }
                    }
gm_ll61:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb57; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc57; }
                    }
                    goto gm_lad;
lb57:;
                }
            }
            goto ix52;
ie52:;
ix52:;
        }
        goto me3;
mn19:;
ma6:;
        uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t2) + 64) + 0));
        if (t82 != ((uint8_t)2ULL)) goto mn81;
        void * t83 = GM_LD(void *, ((((uint8_t *)t2) + 64) + 8));
        gm_l12 = t83;
        {
            void * t85 = gm_l0;
            void * t86 = gm_l12;
            uint32_t t87 = GM_LD(uint32_t, ((((uint8_t *)t86) + 80) + 8));
            gm_b0_1 t88 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t85, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t90 = gm_l12;
            if (!(((((uint8_t *)t90) + 0))[16] != 0)) goto ie89;
            gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)t90) + 0));
            gm_l13 = t91;
            {
                void * t93 = gm_l0;
                gm_b16_8 t94 = gm_l13;
                gm_b16_8 t95 = t94;
                gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
                gm_b0_1 t97 = gm_f_params_ufsghpd((void *)NULL, cx, NULL, t93, t96, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix89;
ie89:;
ix89:;
        }
        {
            void * t98 = gm_l0;
            void * t99 = gm_l12;
            void * t100 = GM_LD(void *, (((uint8_t *)t99) + 24));
            gm_b0_1 t101 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t98, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t102 = gm_l0;
            void * t103 = gm_l12;
            void * t104 = GM_LD(void *, (((uint8_t *)t103) + 32));
            gm_b0_1 t105 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t102, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t107 = gm_l12;
            if (!(((((uint8_t *)t107) + 40))[16] != 0)) goto ie106;
            gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)t107) + 40));
            gm_l14 = t108;
            {
                {
                    gm_l16 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    gm_l15 = t111;
                }
                {
                    for (;;) {
                        uint64_t t113 = gm_l16;
                        uint64_t t114 = gm_l15;
                        uint8_t t115 = (t113 < t114);
                        if (!t115) goto lb112;
                        {
                            uint64_t t118 = gm_l16;
                            uint64_t t119 = t118;
                            uint64_t t122 = GM_LD(uint64_t, ((uint8_t *)&t119));
                            gm_l17 = t122;
                            {
                                void * t124 = gm_l0;
                                uint64_t t125 = gm_l17;
                                uint64_t t126 = (uintptr_t)t125;
                                uint64_t t127 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                if (t126 >= (uintptr_t)(t127)) gm_trap_index(t126, t127, gm_src94heo671ss5e, 209, 51);
                                void * t128 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t126 * 136);
                                gm_b0_1 t129 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t124, t128);
                                if (gm_unw.kind) {
                                    goto gm_ll116;
                                }
                            }
                            goto me120;
mn121:;
                            gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 209, 17);
me120:;
                        }
lc112:;
                        {
                            uint64_t t131 = gm_l16;
                            uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
                            uint64_t t134 = r132;
                            gm_l16 = t134;
                        }
                    }
gm_ll116:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb112; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc112; }
                    }
                    goto gm_lad;
lb112:;
                }
            }
            goto ix106;
ie106:;
ix106:;
        }
        {
            gm_tf5srh4mp4b3i t135;
            memset(&t135, 0, sizeof t135);
            gm_l18 = t135;
        }
        {
            void * t137 = gm_l0;
            void * t138 = gm_l12;
            void * t139 = GM_LD(void *, (((uint8_t *)t138) + 24));
            gm_tu0a56t4bfvqh t140 = gm_f_classify_pdaj2gb((void *)NULL, cx, NULL, t137, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tu0a56t4bfvqh t141 = t140;
            uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&t141) + 0));
            if (t142 != ((uint8_t)2ULL)) goto ie136;
            uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&t141) + 8));
            gm_l19 = t143;
            {
                uint64_t t145 = gm_l19;
                gm_tf5srh4mp4b3i t146;
                GM_ST(uint8_t, (uint8_t *)&t146 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t146 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t146 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t146), t145);
                ((uint8_t *)&t146)[8] = 1;
                gm_l18 = t146;
            }
            goto ix136;
ie136:;
ix136:;
        }
        {
            void * t147 = gm_l0;
            void * t148 = gm_l12;
            gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)t148) + 64));
            gm_b16_8 t150 = t149;
            gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
            gm_tf5srh4mp4b3i t152 = gm_l18;
            gm_b0_1 t153 = gm_f_clauses_posk098((void *)NULL, cx, NULL, t147, t151, t152);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t154 = gm_l0;
            gm_f_pop_scope_vgka5ae(t154);
        }
        {
            void * t155 = gm_l0;
            void * t156 = GM_LD(void *, (((uint8_t *)t155) + 0));
            void * t157 = gm_l1;
            gm_tl874t2atnlh3 t158 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t157) + 4));
            gm_b16_8 t159 = gm_f_src_fjcc76p(t156, GM_CONV(gm_cseahvg8v7p0vs, t158));
            gm_l20 = t159;
        }
        {
            gm_b16_8 t160 = gm_l20;
            gm_l21 = t160;
        }
        {
            gm_b16_8 t162 = gm_l20;
            gm_tf5srh4mp4b3i t163 = gm_f_index_of_hdeg351(t162, ((uint8_t)123ULL));
            gm_tf5srh4mp4b3i t164 = t163;
            if (!((((uint8_t *)&t164))[8] != 0)) goto ie161;
            uint64_t t165 = GM_LD(uint64_t, ((uint8_t *)&t164));
            gm_l22 = t165;
            {
                uint64_t t167 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                void * t168 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)));
                uint64_t t169 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t170 = gm_l22;
                uint64_t t171 = (uintptr_t)t170;
                if (t169 > t171 || t171 > (uintptr_t)t167) gm_trap_slice(t169, t171, t167, gm_src94heo671ss5e, 217, 49);
                gm_b16_8 t172;
                GM_ST(void *, (uint8_t *)&t172, (void *)(((uint8_t *)t168 + t169 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(t171 - t169));
                gm_l21 = t172;
            }
            goto ix161;
ie161:;
ix161:;
        }
        {
            void * t173 = gm_l2;
            gm_t80lhgi01aeei t174;
            memset((uint8_t *)&t174 + 17, 0, 7);
            gm_b16_8 t175 = gm_l21;
            gm_b16_8 t176 = gm_f_trim_fdrfe6i(t175);
            GM_ST(gm_b16_8, (uint8_t *)&t174 + 0, t176);
            gm_t9qkh0rsooc8d t177;
            GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)6ULL));
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t174 + 16, t177);
            gm_b16_8 t178;
            GM_ST(void *, (uint8_t *)&t178, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(0));
            GM_ST(gm_b16_8, (uint8_t *)&t174 + 24, t178);
            void * t179 = gm_l1;
            gm_tl874t2atnlh3 t180 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t179) + 4));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t174 + 40, t180);
            void * t181 = gm_l12;
            void * t182 = GM_LD(void *, (((uint8_t *)t181) + 24));
            gm_tl874t2atnlh3 t183 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t182) + 120));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t174 + 52, t183);
            gm_b16_8 t184 = gm_f_empty_slice_6ld1tun();
            GM_ST(gm_b16_8, (uint8_t *)&t174 + 64, t184);
            gm_tftb1odc739t5 t185 = gm_f_push_le0hqcb(t173, &t174);
            gm_tftb1odc739t5 t186 = t185;
            if (((((uint8_t *)&t186))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t186) + 1, gm_src94heo671ss5e, 218, 13);
            gm_b0_1 t187 = GM_LD(gm_b0_1, (((uint8_t *)&t186) + 1));
        }
        goto me3;
mn81:;
ma7:;
        uint8_t t189 = GM_LD(uint8_t, ((((uint8_t *)t2) + 64) + 0));
        if (t189 != ((uint8_t)3ULL)) goto mn188;
        gm_tcfiotghd1s5k t190 = GM_LD(gm_tcfiotghd1s5k, ((((uint8_t *)t2) + 64) + 8));
        F.l23 = t190;
        {
            void * t192 = gm_l0;
            void * t193 = (void *)(((uint8_t *)&F.l23) + 40);
            gm_b0_1 t194 = gm_f_block_jfodv9u((void *)NULL, cx, NULL, t192, t193);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t195;
            uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&F.l23) + 80));
            if (!t197) goto ie196;
            gm_b16_8 t199;
            GM_ST(void *, (uint8_t *)&t199, (void *)(gm_srfagufvviad7));
            GM_ST(uintptr_t, (uint8_t *)&t199 + 8, (uintptr_t)(5));
            gm_b16_8 t200 = t199;
            t195 = t200;
            goto ix196;
ie196:;
            gm_b16_8 t202;
            GM_ST(void *, (uint8_t *)&t202, (void *)(gm_s9r64t1166q1f));
            GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(4));
            gm_b16_8 t203 = t202;
            t195 = t203;
ix196:;
            gm_l24 = t195;
        }
        {
            void * t204 = gm_l2;
            gm_t80lhgi01aeei t205;
            memset((uint8_t *)&t205 + 17, 0, 7);
            gm_t6j6smj7qoctu t206;
            gm_b16_8 t207 = gm_l24;
            GM_ST(gm_b16_8, (uint8_t *)&t206 + 0, t207);
            gm_b16_8 t208 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l23) + 0) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t206 + 16, t208);
            gm_tabb710d9mq2e t209;
            gm_f_format_jf46ta8((void *)&t209, (void *)NULL, cx, NULL, &t206);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t210 = t209;
            if (((((uint8_t *)&t210))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t210) + 8, gm_src94heo671ss5e, 231, 23);
            gm_b16_8 t211 = GM_LD(gm_b16_8, (((uint8_t *)&t210) + 8));
            gm_b16_8 t212 = t211;
            gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&t212));
            GM_ST(gm_b16_8, (uint8_t *)&t205 + 0, t213);
            gm_t9qkh0rsooc8d t214;
            GM_ST(uint8_t, ((uint8_t *)&t214 + 0), ((uint8_t)13ULL));
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t205 + 16, t214);
            gm_b16_8 t215;
            GM_ST(void *, (uint8_t *)&t215, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(0));
            GM_ST(gm_b16_8, (uint8_t *)&t205 + 24, t215);
            void * t216 = gm_l1;
            gm_tl874t2atnlh3 t217 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t216) + 4));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t205 + 40, t217);
            gm_tl874t2atnlh3 t218 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l23) + 24));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t205 + 52, t218);
            gm_b16_8 t219 = gm_f_empty_slice_6ld1tun();
            GM_ST(gm_b16_8, (uint8_t *)&t205 + 64, t219);
            gm_tftb1odc739t5 t220 = gm_f_push_le0hqcb(t204, &t205);
            gm_tftb1odc739t5 t221 = t220;
            if (((((uint8_t *)&t221))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t221) + 1, gm_src94heo671ss5e, 230, 13);
            gm_b0_1 t222 = GM_LD(gm_b0_1, (((uint8_t *)&t221) + 1));
        }
        goto me3;
mn188:;
ma8:;
        uint8_t t224 = GM_LD(uint8_t, ((((uint8_t *)t2) + 64) + 0));
        if (t224 != ((uint8_t)4ULL)) goto mn223;
        void * t225 = GM_LD(void *, ((((uint8_t *)t2) + 64) + 8));
        gm_l25 = t225;
        void * t226 = gm_l0;
        void * t227 = gm_l25;
        gm_b0_1 t228 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t226, t227);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn223:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 194, 5);
me3:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_params_ufsghpd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_params_ufsghpd F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* ps */
    uint8_t gm_l2; /* declare */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    void * gm_l6; /* p */
    void * gm_l7; /* t */
    void * gm_l8; /* d */
    gm_b16_8 gm_l9; /* ty */
    void * gm_l10; /* t */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t08olq97rscn3 gm_l13; /* n */
    uint8_t gm_l14; /* is_type */
    void * gm_l15; /* t */
    gm_t08olq97rscn3 gm_l16; /* id */
    gm_t9qkh0rsooc8d gm_l17; /* kind */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_t08olq97rscn3 gm_l20; /* n */
    GM_STACK_CHECK(&F, 1121u);
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
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l3 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = gm_l3;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = t10;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_src94heo671ss5e, 41, 15);
                        void * t19 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t17 * 56);
                        gm_l6 = t19;
                    }
                    {
                        void * t21 = gm_l6;
                        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 24));
                        void * t23 = t22;
                        if (!(GM_LD(void *, ((uint8_t *)&t23)) != 0)) goto ie20;
                        void * t24 = GM_LD(void *, ((uint8_t *)&t23));
                        gm_l7 = t24;
                        {
                            void * t26 = gm_l0;
                            void * t27 = gm_l7;
                            gm_b0_1 t28 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t26, t27);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto ix20;
ie20:;
ix20:;
                    }
                    {
                        void * t30 = gm_l6;
                        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 32));
                        void * t32 = t31;
                        if (!(GM_LD(void *, ((uint8_t *)&t32)) != 0)) goto ie29;
                        void * t33 = GM_LD(void *, ((uint8_t *)&t32));
                        gm_l8 = t33;
                        {
                            void * t35 = gm_l0;
                            void * t36 = gm_l8;
                            gm_b0_1 t37 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t35, t36);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto ix29;
ie29:;
ix29:;
                    }
                    {
                        uint8_t t39 = gm_l2;
                        uint8_t t40 = (uint8_t)!t39;
                        if (!t40) goto ie38;
                        {
                            gm_b16_8 t42;
                            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(0));
                            gm_l9 = t42;
                        }
                        {
                            void * t44 = gm_l6;
                            void * t45 = GM_LD(void *, (((uint8_t *)t44) + 24));
                            void * t46 = t45;
                            if (!(GM_LD(void *, ((uint8_t *)&t46)) != 0)) goto ie43;
                            void * t47 = GM_LD(void *, ((uint8_t *)&t46));
                            gm_l10 = t47;
                            {
                                void * t49 = gm_l0;
                                void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
                                void * t51 = gm_l10;
                                gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t51) + 120));
                                gm_b16_8 t53 = gm_f_src_fjcc76p(t50, GM_CONV(gm_cseahvg8v7p0vs, t52));
                                gm_l9 = t53;
                            }
                            goto ix43;
ie43:;
ix43:;
                        }
                        {
                            {
                                void * t55 = gm_l6;
                                gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)t55) + 0));
                                gm_l11 = t56;
                            }
                            {
                                gm_l12 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t58 = gm_l12;
                                    uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                    uint8_t t60 = (t58 < t59);
                                    if (!t60) goto lb57;
                                    {
                                        uint64_t t63 = gm_l12;
                                        uint64_t t64 = (uintptr_t)t63;
                                        uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                        gm_t08olq97rscn3 t68 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t64 * 32));
                                        gm_l13 = t68;
                                        {
                                            void * t70 = gm_l0;
                                            gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 16));
                                            gm_t6ok5tvcjc200 t72;
                                            gm_t6j6smj7qoctu t73;
                                            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t73 + 0, t74);
                                            gm_b16_8 t75 = gm_l9;
                                            GM_ST(gm_b16_8, (uint8_t *)&t73 + 16, t75);
                                            gm_tabb710d9mq2e t76;
                                            gm_f_format_f6rro2t((void *)&t76, (void *)NULL, cx, NULL, &t73);
                                            if (gm_unw.kind) {
                                                goto gm_ll61;
                                            }
                                            gm_tabb710d9mq2e t77 = t76;
                                            if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_src94heo671ss5e, 49, 48);
                                            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
                                            gm_b16_8 t79 = t78;
                                            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                                            GM_ST(gm_b16_8, (uint8_t *)&t72 + 0, t80);
                                            gm_th2lqbo77pvsc t81;
                                            memset(&t81, 0, sizeof t81);
                                            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t72 + 16, t81);
                                            gm_b0_1 t82 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t70, t71, &t72);
                                            if (gm_unw.kind) {
                                                goto gm_ll61;
                                            }
                                        }
                                        goto me66;
mn67:;
                                        gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 48, 13);
me66:;
                                    }
lc57:;
                                    {
                                        uint64_t t84 = gm_l12;
                                        uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                                        uint64_t t87 = r85;
                                        gm_l12 = t87;
                                    }
                                }
gm_ll61:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb57; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc57; }
                                }
                                goto gm_ll8;
lb57:;
                            }
                        }
                        {
                            goto lc4;
                        }
                        goto ix38;
ie38:;
ix38:;
                    }
                    {
                        gm_l14 = ((uint8_t)0);
                    }
                    {
                        void * t89 = gm_l6;
                        void * t90 = GM_LD(void *, (((uint8_t *)t89) + 24));
                        void * t91 = t90;
                        if (!(GM_LD(void *, ((uint8_t *)&t91)) != 0)) goto ie88;
                        void * t92 = GM_LD(void *, ((uint8_t *)&t91));
                        gm_l15 = t92;
                        {
                            void * t95 = gm_l15;
                            uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)t95) + 8) + 0));
                            if (t96 != ((uint8_t)6ULL)) goto ie94;
                            gm_t08olq97rscn3 t97 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t95) + 8) + 8));
                            gm_l16 = t97;
                            {
                                uint8_t t99 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l16) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l16) + 0)), gm_s2737i1iq57nf, 4) == 0));
                                gm_l14 = t99;
                            }
                            goto ix94;
ie94:;
ix94:;
                        }
                        goto ix88;
ie88:;
ix88:;
                    }
                    {
                        gm_t9qkh0rsooc8d t100;
                        uint8_t t102 = gm_l14;
                        if (!t102) goto ie101;
                        gm_t9qkh0rsooc8d t104;
                        GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)15ULL));
                        gm_t9qkh0rsooc8d t105 = t104;
                        t100 = t105;
                        goto ix101;
ie101:;
                        gm_t9qkh0rsooc8d t106;
                        void * t108 = gm_l6;
                        uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)t108) + 16));
                        if (!t109) goto ie107;
                        gm_t9qkh0rsooc8d t111;
                        GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)7ULL));
                        gm_t9qkh0rsooc8d t112 = t111;
                        t106 = t112;
                        goto ix107;
ie107:;
                        gm_t9qkh0rsooc8d t114;
                        GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)9ULL));
                        gm_t9qkh0rsooc8d t115 = t114;
                        t106 = t115;
ix107:;
                        t100 = t106;
ix101:;
                        gm_l17 = t100;
                    }
                    {
                        {
                            void * t117 = gm_l6;
                            gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)t117) + 0));
                            gm_l18 = t118;
                        }
                        {
                            gm_l19 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t120 = gm_l19;
                                uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                uint8_t t122 = (t120 < t121);
                                if (!t122) goto lb119;
                                {
                                    uint64_t t125 = gm_l19;
                                    uint64_t t126 = (uintptr_t)t125;
                                    uint64_t t127 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                    gm_t08olq97rscn3 t130 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t126 * 32));
                                    gm_l20 = t130;
                                    {
                                        void * t132 = gm_l0;
                                        gm_t9qkh0rsooc8d t133 = gm_l17;
                                        uint32_t t134 = GM_LD(uint32_t, ((((uint8_t *)&gm_l20) + 16) + 8));
                                        gm_b0_1 t135 = gm_f_declare_feg3ue0((void *)NULL, cx, NULL, t132, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l20)), t133, t134);
                                        if (gm_unw.kind) {
                                            goto gm_ll123;
                                        }
                                    }
                                    goto me128;
mn129:;
                                    gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 58, 9);
me128:;
                                }
lc119:;
                                {
                                    uint64_t t137 = gm_l19;
                                    uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                                    uint64_t t140 = r138;
                                    gm_l19 = t140;
                                }
                            }
gm_ll123:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb119; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc119; }
                            }
                            goto gm_ll8;
lb119:;
                        }
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 40, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t142 = gm_l4;
                    uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                    uint64_t t145 = r143;
                    gm_l4 = t145;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_sig_89f514c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_sig_89f514c F;
    void * gm_l0; /* w */
    void * gm_l1; /* sg */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        uint32_t t4 = GM_LD(uint32_t, ((((uint8_t *)t3) + 56) + 8));
        gm_b0_1 t5 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t2, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l1;
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 0));
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_b0_1 t11 = gm_f_params_ufsghpd((void *)NULL, cx, NULL, t6, t10, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l1;
        gm_b0_1 t14 = gm_f_sig_rest_gkcl8d2((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t15 = gm_l0;
        gm_f_pop_scope_vgka5ae(t15);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_sig_rest_gkcl8d2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_sig_rest_gkcl8d2 F;
    void * gm_l0; /* w */
    void * gm_l1; /* sg */
    void * gm_l2; /* r */
    gm_b16_8 gm_l3; /* effects */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 241u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 24));
        void * t5 = t4;
        if (!(GM_LD(void *, ((uint8_t *)&t5)) != 0)) goto ie2;
        void * t6 = GM_LD(void *, ((uint8_t *)&t5));
        gm_l2 = t6;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l2;
            gm_b0_1 t10 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l1;
        if (!(((((uint8_t *)t12) + 32))[16] != 0)) goto ie11;
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 32));
        gm_l3 = t13;
        {
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l4 = t16;
            }
            {
                for (;;) {
                    uint64_t t18 = gm_l5;
                    uint64_t t19 = gm_l4;
                    uint8_t t20 = (t18 < t19);
                    if (!t20) goto lb17;
                    {
                        uint64_t t23 = gm_l5;
                        uint64_t t24 = t23;
                        uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&t24));
                        gm_l6 = t27;
                        {
                            void * t29 = gm_l0;
                            uint64_t t30 = gm_l6;
                            uint64_t t31 = (uintptr_t)t30;
                            uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_src94heo671ss5e, 34, 43);
                            void * t33 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t31 * 136);
                            gm_b0_1 t34 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t29, t33);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                        }
                        goto me25;
mn26:;
                        gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 34, 9);
me25:;
                    }
lc17:;
                    {
                        uint64_t t36 = gm_l5;
                        uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                        uint64_t t39 = r37;
                        gm_l5 = t39;
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
        goto ix11;
ie11:;
ix11:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_struct_members_0n9oe2e(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_struct_members_0n9oe2e F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* members */
    void * gm_l2; /* out */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    void * gm_l7; /* d */
    gm_b16_8 gm_l8; /* text */
    gm_b16_8 gm_l9; /* ty */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t08olq97rscn3 gm_l12; /* n */
    gm_b16_8 gm_l14; /* e */
    GM_STACK_CHECK(&F, 1093u);
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
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l3 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = gm_l3;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = t10;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_src94heo671ss5e, 136, 15);
                        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t17 * 80) + 0));
                        if (t21 != ((uint8_t)0ULL)) goto mn20;
                        gm_t24301fas1b2a t22 = GM_LD(gm_t24301fas1b2a, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t17 * 80) + 8));
                        F.l6 = t22;
                        {
                            void * t24 = gm_l0;
                            void * t25 = GM_LD(void *, (((uint8_t *)&F.l6) + 40));
                            gm_b0_1 t26 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t24, t25);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t28 = GM_LD(void *, (((uint8_t *)&F.l6) + 48));
                            void * t29 = t28;
                            if (!(GM_LD(void *, ((uint8_t *)&t29)) != 0)) goto ie27;
                            void * t30 = GM_LD(void *, ((uint8_t *)&t29));
                            gm_l7 = t30;
                            {
                                void * t32 = gm_l0;
                                void * t33 = gm_l7;
                                gm_b0_1 t34 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t32, t33);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            goto ix27;
ie27:;
ix27:;
                        }
                        {
                            void * t35 = gm_l0;
                            void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
                            gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 56));
                            gm_b16_8 t38 = gm_f_src_fjcc76p(t36, GM_CONV(gm_cseahvg8v7p0vs, t37));
                            gm_b16_8 t39 = gm_f_trim_fdrfe6i(t38);
                            gm_l8 = t39;
                        }
                        {
                            void * t40 = gm_l0;
                            void * t41 = GM_LD(void *, (((uint8_t *)t40) + 0));
                            void * t42 = GM_LD(void *, (((uint8_t *)&F.l6) + 40));
                            gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t42) + 120));
                            gm_b16_8 t44 = gm_f_src_fjcc76p(t41, GM_CONV(gm_cseahvg8v7p0vs, t43));
                            gm_l9 = t44;
                        }
                        {
                            {
                                gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 24));
                                gm_l10 = t46;
                            }
                            {
                                gm_l11 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t48 = gm_l11;
                                    uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                    uint8_t t50 = (t48 < t49);
                                    if (!t50) goto lb47;
                                    {
                                        uint64_t t53 = gm_l11;
                                        uint64_t t54 = (uintptr_t)t53;
                                        uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                        gm_t08olq97rscn3 t58 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t54 * 32));
                                        gm_l12 = t58;
                                        {
                                            void * t60 = gm_l0;
                                            gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l12) + 16));
                                            gm_t6ok5tvcjc200 t62;
                                            gm_b16_8 t63 = gm_l8;
                                            GM_ST(gm_b16_8, (uint8_t *)&t62 + 0, t63);
                                            gm_th2lqbo77pvsc t64;
                                            memset(&t64, 0, sizeof t64);
                                            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t62 + 16, t64);
                                            gm_b0_1 t65 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t60, t61, &t62);
                                            if (gm_unw.kind) {
                                                goto gm_ll51;
                                            }
                                        }
                                        {
                                            void * t66 = gm_l2;
                                            gm_t80lhgi01aeei t67;
                                            memset((uint8_t *)&t67 + 17, 0, 7);
                                            gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t67 + 0, t68);
                                            gm_t9qkh0rsooc8d t69;
                                            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)10ULL));
                                            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t67 + 16, t69);
                                            gm_b16_8 t70 = gm_l9;
                                            GM_ST(gm_b16_8, (uint8_t *)&t67 + 24, t70);
                                            gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 56));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t67 + 40, t71);
                                            gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l12) + 16));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t67 + 52, t72);
                                            gm_b16_8 t73 = gm_f_empty_slice_6ld1tun();
                                            GM_ST(gm_b16_8, (uint8_t *)&t67 + 64, t73);
                                            gm_tftb1odc739t5 t74 = gm_f_push_le0hqcb(t66, &t67);
                                            gm_tftb1odc739t5 t75 = t74;
                                            if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_src94heo671ss5e, 144, 21);
                                            gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
                                        }
                                        goto me56;
mn57:;
                                        gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 142, 17);
me56:;
                                    }
lc47:;
                                    {
                                        uint64_t t78 = gm_l11;
                                        uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                                        uint64_t t81 = r79;
                                        gm_l11 = t81;
                                    }
                                }
gm_ll51:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb47; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc47; }
                                }
                                goto gm_ll8;
lb47:;
                            }
                        }
                        goto me19;
mn20:;
                        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t17 * 80) + 0));
                        if (t83 != ((uint8_t)1ULL)) goto mn82;
                        gm_t7bjbmukv26e7 t84 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t17 * 80) + 8));
                        F.l13 = t84;
                        {
                            void * t86 = gm_l0;
                            void * t87 = GM_LD(void *, (((uint8_t *)&F.l13) + 0));
                            gm_b0_1 t88 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t86, t87);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t89 = gm_l0;
                            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 8));
                            gm_b16_8 t91 = t90;
                            gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                            void * t93 = gm_l2;
                            gm_b0_1 t94 = gm_f_struct_members_0n9oe2e((void *)NULL, cx, NULL, t89, t92, t93);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            if (!(((((uint8_t *)&F.l13) + 24))[16] != 0)) goto ie95;
                            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 24));
                            gm_l14 = t96;
                            {
                                void * t98 = gm_l0;
                                gm_b16_8 t99 = gm_l14;
                                gm_b16_8 t100 = t99;
                                gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
                                void * t102 = gm_l2;
                                gm_b0_1 t103 = gm_f_struct_members_0n9oe2e((void *)NULL, cx, NULL, t98, t101, t102);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            goto ix95;
ie95:;
ix95:;
                        }
                        goto me19;
mn82:;
                        gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 136, 9);
me19:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 135, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t105 = gm_l4;
                    uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                    uint64_t t108 = r106;
                    gm_l4 = t108;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_top_binding_pp9dk39(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3) {
    struct gm_fr_top_binding_pp9dk39 F;
    void * gm_l0; /* w */
    void * gm_l1; /* it */
    void * gm_l2; /* b */
    void * gm_l3; /* out */
    void * gm_l4; /* t */
    gm_t08olq97rscn3 gm_l5; /* name */
    gm_tf5srh4mp4b3i gm_l6; /* decl */
    uint64_t gm_l7; /* d */
    gm_t9qkh0rsooc8d gm_l8; /* kind */
    gm_b16_8 gm_l9; /* detail */
    uint64_t gm_l10; /* d */
    void * gm_l13; /* init */
    void * gm_l14; /* inner */
    void * gm_l15; /* lit */
    uint64_t gm_l16; /* d */
    void * gm_l17; /* lit */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    uint64_t gm_l21; /* d */
    uint64_t gm_l23; /* m */
    uint32_t gm_l24; /* first */
    void * gm_l25; /* f */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* i */
    void * gm_l29; /* m */
    gm_t08olq97rscn3 gm_l30; /* n */
    GM_STACK_CHECK(&F, 2509u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l2;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 24));
        void * t5 = t4;
        if (!(GM_LD(void *, ((uint8_t *)&t5)) != 0)) goto ie2;
        void * t6 = GM_LD(void *, ((uint8_t *)&t5));
        gm_l4 = t6;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l4;
            gm_b0_1 t10 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l2;
        uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)t12) + 8) + 8);
        uint8_t t14 = (!(t13 == ((uint64_t)1ULL)));
        if (!t14) goto ie11;
        {
            void * t16 = gm_l0;
            void * t17 = gm_l2;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 40));
            gm_b0_1 t19 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t16, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t20;
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        void * t23 = gm_l2;
        uint64_t t24 = GM_LD(uintptr_t, (((uint8_t *)t23) + 8) + 8);
        if (t22 >= (uintptr_t)(t24)) gm_trap_index(t22, t24, gm_src94heo671ss5e, 249, 13);
        gm_t08olq97rscn3 t25 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, (((uint8_t *)t23) + 8)) + t22 * 32));
        gm_l5 = t25;
    }
    {
        gm_tf5srh4mp4b3i t26;
        memset(&t26, 0, sizeof t26);
        gm_l6 = t26;
    }
    {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
        void * t31 = gm_l0;
        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 24));
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
        gm_tf5srh4mp4b3i t34 = gm_f_lookup_59juqoo(t30, t32, t33);
        gm_tf5srh4mp4b3i t35 = t34;
        if (!((((uint8_t *)&t35))[8] != 0)) goto ie27;
        uint64_t t36 = GM_LD(uint64_t, ((uint8_t *)&t35));
        gm_l7 = t36;
        {
            uint64_t t39 = gm_l7;
            uint64_t t40 = (uintptr_t)t39;
            void * t41 = gm_l0;
            void * t42 = GM_LD(void *, (((uint8_t *)t41) + 0));
            void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
            uint64_t t44 = GM_LD(uintptr_t, ((((uint8_t *)t43) + 240) + 0) + 8);
            if (t40 >= (uintptr_t)(t44)) gm_trap_index(t40, t44, gm_src94heo671ss5e, 252, 12);
            gm_tl874t2atnlh3 t45 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t43) + 240) + 0)) + t40 * 216) + 0) + 16));
            gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 16));
            gm_tl874t2atnlh3 t47 = t45;
            gm_tl874t2atnlh3 t48 = t46;
            uint8_t t49 = (gm_eqff85ac10o3(((uint8_t *)&t47), ((uint8_t *)&t48)));
            if (!t49) goto ie38;
            {
                uint64_t t51 = gm_l7;
                gm_tf5srh4mp4b3i t52;
                GM_ST(uint8_t, (uint8_t *)&t52 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t52 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t52 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t52), t51);
                ((uint8_t *)&t52)[8] = 1;
                gm_l6 = t52;
            }
            goto ix38;
ie38:;
ix38:;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        gm_t9qkh0rsooc8d t53;
        GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)7ULL));
        gm_l8 = t53;
    }
    {
        gm_b16_8 t54;
        GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(0));
        gm_l9 = t54;
    }
    {
        if (!((((uint8_t *)&gm_l6))[8] != 0)) goto ie55;
        uint64_t t56 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
        gm_l10 = t56;
        {
            void * t58 = gm_l0;
            uint64_t t59 = gm_l10;
            gm_tsncjtib3j1l1 t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t60 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t60 + 8), t59);
            gm_b0_1 t61 = gm_f_ensure_hover_3epdjmk((void *)NULL, cx, NULL, t58, &t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t62 = gm_l0;
            void * t63 = GM_LD(void *, (((uint8_t *)t62) + 0));
            uint64_t t64 = gm_l10;
            gm_f_decl_item_a57s8if((void *)&F.l11, (void *)NULL, cx, NULL, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t9qkh0rsooc8d t65 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l11) + 16));
            gm_l8 = t65;
        }
        {
            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 24));
            gm_l9 = t66;
        }
        goto ix55;
ie55:;
ix55:;
    }
    {
        gm_f_list_uc1atd0((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t67 = gm_l2;
        void * t68 = GM_LD(void *, (((uint8_t *)t67) + 40));
        gm_l13 = t68;
    }
    {
        for (;;) {
            void * t70 = gm_l13;
            uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t70) + 8) + 0));
            if (t71 != ((uint8_t)35ULL)) goto lb69;
            void * t72 = GM_LD(void *, (((((uint8_t *)t70) + 8) + 8) + 16));
            gm_l14 = t72;
            {
                void * t75 = gm_l14;
                gm_l13 = t75;
            }
lc69:;
        }
lb69:;
    }
    {
        void * t76 = gm_l13;
        uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t76) + 8) + 0));
        switch (t84) { case ((uint8_t)0ULL): goto ma82; case ((uint8_t)1ULL): goto ma82; case ((uint8_t)2ULL): goto ma82; case ((uint8_t)3ULL): goto ma82; case ((uint8_t)4ULL): goto ma82; case ((uint8_t)5ULL): goto ma82; case ((uint8_t)6ULL): goto ma82; case ((uint8_t)7ULL): goto ma82; case ((uint8_t)8ULL): goto ma82; case ((uint8_t)9ULL): goto ma82; case ((uint8_t)10ULL): goto ma82; case ((uint8_t)11ULL): goto ma82; case ((uint8_t)12ULL): goto ma82; case ((uint8_t)13ULL): goto ma82; case ((uint8_t)14ULL): goto ma82; case ((uint8_t)15ULL): goto ma82; case ((uint8_t)16ULL): goto ma82; case ((uint8_t)17ULL): goto ma82; case ((uint8_t)18ULL): goto ma82; case ((uint8_t)19ULL): goto ma82; case ((uint8_t)20ULL): goto ma82; case ((uint8_t)21ULL): goto ma82; case ((uint8_t)22ULL): goto ma82; case ((uint8_t)23ULL): goto ma82; case ((uint8_t)24ULL): goto ma82; case ((uint8_t)25ULL): goto ma82; case ((uint8_t)26ULL): goto ma82; case ((uint8_t)27ULL): goto ma82; case ((uint8_t)28ULL): goto ma82; case ((uint8_t)29ULL): goto ma82; case ((uint8_t)30ULL): goto ma82; case ((uint8_t)31ULL): goto ma82; case ((uint8_t)32ULL): goto ma79; case ((uint8_t)33ULL): goto ma79; case ((uint8_t)34ULL): goto ma79; case ((uint8_t)35ULL): goto ma82; case ((uint8_t)36ULL): goto ma78; case ((uint8_t)37ULL): goto ma82; case ((uint8_t)38ULL): goto ma80; case ((uint8_t)39ULL): goto ma81; case ((uint8_t)40ULL): goto ma82; case ((uint8_t)41ULL): goto ma82; case ((uint8_t)42ULL): goto ma82; case ((uint8_t)43ULL): goto ma82; case ((uint8_t)44ULL): goto ma82; case ((uint8_t)45ULL): goto ma82; case ((uint8_t)46ULL): goto ma82; case ((uint8_t)47ULL): goto ma82; case ((uint8_t)48ULL): goto ma82; case ((uint8_t)49ULL): goto ma82; case ((uint8_t)50ULL): goto ma82; default: goto ma82; }
ma78:;
        uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t76) + 8) + 0));
        if (t86 != ((uint8_t)36ULL)) goto mn85;
        void * t87 = GM_LD(void *, ((((uint8_t *)t76) + 8) + 8));
        gm_l15 = t87;
        {
            uint8_t t90;
            if (((((uint8_t *)&gm_l6))[8] != 0)) goto mn92;
            t90 = ((uint8_t)1);
            goto me91;
mn92:;
            t90 = ((uint8_t)0);
            goto me91;
mn93:;
            gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 267, 16);
me91:;
            if (!t90) goto ie89;
            {
                gm_t9qkh0rsooc8d t95;
                GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)1ULL));
                gm_l8 = t95;
            }
            goto ix89;
ie89:;
ix89:;
        }
        {
            void * t96 = gm_l0;
            void * t97 = gm_l15;
            gm_b0_1 t98 = gm_f_fn_literal_sodo3l0((void *)NULL, cx, NULL, t96, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me77;
mn85:;
ma79:;
        uint8_t t102 = GM_LD(uint8_t, ((((uint8_t *)t76) + 8) + 0));
        if (t102 != ((uint8_t)32ULL)) goto pa101;
        goto po100;
pa101:;
        uint8_t t104 = GM_LD(uint8_t, ((((uint8_t *)t76) + 8) + 0));
        if (t104 != ((uint8_t)33ULL)) goto pa103;
        goto po100;
pa103:;
        uint8_t t106 = GM_LD(uint8_t, ((((uint8_t *)t76) + 8) + 0));
        if (t106 != ((uint8_t)34ULL)) goto pa105;
        goto po100;
pa105:;
        goto mn99;
po100:;
        {
            uint8_t t109;
            if (((((uint8_t *)&gm_l6))[8] != 0)) goto mn111;
            t109 = ((uint8_t)1);
            goto me110;
mn111:;
            t109 = ((uint8_t)0);
            goto me110;
mn112:;
            gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 271, 16);
me110:;
            if (!t109) goto ie108;
            {
                void * t114 = gm_l13;
                gm_t9qkh0rsooc8d t115;
                uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t114) + 8) + 0));
                switch (t121) { case ((uint8_t)0ULL): goto ma119; case ((uint8_t)1ULL): goto ma119; case ((uint8_t)2ULL): goto ma119; case ((uint8_t)3ULL): goto ma119; case ((uint8_t)4ULL): goto ma119; case ((uint8_t)5ULL): goto ma119; case ((uint8_t)6ULL): goto ma119; case ((uint8_t)7ULL): goto ma119; case ((uint8_t)8ULL): goto ma119; case ((uint8_t)9ULL): goto ma119; case ((uint8_t)10ULL): goto ma119; case ((uint8_t)11ULL): goto ma119; case ((uint8_t)12ULL): goto ma119; case ((uint8_t)13ULL): goto ma119; case ((uint8_t)14ULL): goto ma119; case ((uint8_t)15ULL): goto ma119; case ((uint8_t)16ULL): goto ma119; case ((uint8_t)17ULL): goto ma119; case ((uint8_t)18ULL): goto ma119; case ((uint8_t)19ULL): goto ma119; case ((uint8_t)20ULL): goto ma119; case ((uint8_t)21ULL): goto ma119; case ((uint8_t)22ULL): goto ma119; case ((uint8_t)23ULL): goto ma119; case ((uint8_t)24ULL): goto ma119; case ((uint8_t)25ULL): goto ma119; case ((uint8_t)26ULL): goto ma119; case ((uint8_t)27ULL): goto ma119; case ((uint8_t)28ULL): goto ma119; case ((uint8_t)29ULL): goto ma119; case ((uint8_t)30ULL): goto ma119; case ((uint8_t)31ULL): goto ma119; case ((uint8_t)32ULL): goto ma117; case ((uint8_t)33ULL): goto ma118; case ((uint8_t)34ULL): goto ma119; case ((uint8_t)35ULL): goto ma119; case ((uint8_t)36ULL): goto ma119; case ((uint8_t)37ULL): goto ma119; case ((uint8_t)38ULL): goto ma119; case ((uint8_t)39ULL): goto ma119; case ((uint8_t)40ULL): goto ma119; case ((uint8_t)41ULL): goto ma119; case ((uint8_t)42ULL): goto ma119; case ((uint8_t)43ULL): goto ma119; case ((uint8_t)44ULL): goto ma119; case ((uint8_t)45ULL): goto ma119; case ((uint8_t)46ULL): goto ma119; case ((uint8_t)47ULL): goto ma119; case ((uint8_t)48ULL): goto ma119; case ((uint8_t)49ULL): goto ma119; case ((uint8_t)50ULL): goto ma119; default: goto ma119; }
ma117:;
                uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t114) + 8) + 0));
                if (t123 != ((uint8_t)32ULL)) goto mn122;
                gm_t9qkh0rsooc8d t124;
                GM_ST(uint8_t, ((uint8_t *)&t124 + 0), ((uint8_t)3ULL));
                t115 = t124;
                goto me116;
mn122:;
ma118:;
                uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)t114) + 8) + 0));
                if (t126 != ((uint8_t)33ULL)) goto mn125;
                gm_t9qkh0rsooc8d t127;
                GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)4ULL));
                t115 = t127;
                goto me116;
mn125:;
ma119:;
                gm_t9qkh0rsooc8d t129;
                GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)5ULL));
                t115 = t129;
                goto me116;
mn128:;
ma120:;
                gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 272, 24);
me116:;
                gm_l8 = t115;
            }
            goto ix108;
ie108:;
ix108:;
        }
        {
            void * t130 = gm_l0;
            void * t131 = gm_l13;
            void * t132 = (void *)((uint8_t *)&F.l12);
            gm_b0_1 t133 = gm_f_type_lit_lm0h7k6((void *)NULL, cx, NULL, t130, t131, t132);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t134;
            GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(0));
            gm_l9 = t134;
        }
        {
            if (!((((uint8_t *)&gm_l6))[8] != 0)) goto ie135;
            uint64_t t136 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
            gm_l16 = t136;
            {
                void * t139 = gm_l13;
                uint8_t t140 = GM_LD(uint8_t, ((((uint8_t *)t139) + 8) + 0));
                if (t140 != ((uint8_t)34ULL)) goto ie138;
                void * t141 = GM_LD(void *, ((((uint8_t *)t139) + 8) + 8));
                gm_l17 = t141;
                {
                    {
                        gm_l19 = ((uint64_t)0ULL);
                    }
                    {
                        void * t144 = gm_l17;
                        uint64_t t145 = GM_LD(uintptr_t, (((uint8_t *)t144) + 24) + 8);
                        gm_l18 = t145;
                    }
                    {
                        for (;;) {
                            uint64_t t147 = gm_l19;
                            uint64_t t148 = gm_l18;
                            uint8_t t149 = (t147 < t148);
                            if (!t149) goto lb146;
                            {
                                uint64_t t152 = gm_l19;
                                uint64_t t153 = t152;
                                uint64_t t156 = GM_LD(uint64_t, ((uint8_t *)&t153));
                                gm_l20 = t156;
                                {
                                    void * t158 = gm_l0;
                                    uint64_t t159 = gm_l16;
                                    uint64_t t160 = gm_l20;
                                    uint32_t t161 = (uint32_t)t160;
                                    gm_tn68el8d14vku t162;
                                    GM_ST(uint32_t, (uint8_t *)&t162 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t162 + 0), t159);
                                    GM_ST(uint32_t, ((uint8_t *)&t162 + 8), t161);
                                    gm_tsncjtib3j1l1 t163;
                                    GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)1ULL));
                                    GM_ST(gm_tn68el8d14vku, ((uint8_t *)&t163 + 8), t162);
                                    gm_b0_1 t164 = gm_f_ensure_hover_3epdjmk((void *)NULL, cx, NULL, t158, &t163);
                                    if (gm_unw.kind) {
                                        goto gm_ll150;
                                    }
                                }
                                goto me154;
mn155:;
                                gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 283, 21);
me154:;
                            }
lc146:;
                            {
                                uint64_t t166 = gm_l19;
                                uint64_t r167; bool o168 = __builtin_add_overflow(t166, ((uint64_t)1ULL), &r167);
                                uint64_t t169 = r167;
                                gm_l19 = t169;
                            }
                        }
gm_ll150:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb146; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc146; }
                        }
                        goto gm_lad;
lb146:;
                    }
                }
                goto ix138;
ie138:;
ix138:;
            }
            goto ix135;
ie135:;
ix135:;
        }
        goto me77;
mn99:;
ma80:;
        uint8_t t171 = GM_LD(uint8_t, ((((uint8_t *)t76) + 8) + 0));
        if (t171 != ((uint8_t)38ULL)) goto mn170;
        {
            void * t173 = gm_l0;
            void * t174 = gm_l2;
            void * t175 = GM_LD(void *, (((uint8_t *)t174) + 40));
            gm_b0_1 t176 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t173, t175);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            if (!((((uint8_t *)&gm_l6))[8] != 0)) goto ie177;
            uint64_t t178 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
            gm_l21 = t178;
            {
                void * t181 = gm_l0;
                void * t182 = GM_LD(void *, (((uint8_t *)t181) + 0));
                uint64_t t183 = gm_l21;
                gm_tlabdfckddbfd t184;
                gm_f_decl_done_mk70ol9((void *)&t184, (void *)NULL, cx, NULL, t182, t183);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tlabdfckddbfd t185 = t184;
                if (!((((uint8_t *)&t185))[208] != 0)) goto ie180;
                gm_t6qooom8qa2do t186 = GM_LD(gm_t6qooom8qa2do, ((uint8_t *)&t185));
                F.l22 = t186;
                {
                    uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
                    if (t189 != ((uint8_t)9ULL)) goto ie188;
                    uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 8));
                    gm_l23 = t190;
                    {
                        gm_t9qkh0rsooc8d t192;
                        GM_ST(uint8_t, ((uint8_t *)&t192 + 0), ((uint8_t)0ULL));
                        gm_l8 = t192;
                    }
                    {
                        void * t194 = gm_l0;
                        uint64_t t195 = gm_l23;
                        gm_t9805qtonbsag t196 = gm_f_first_file_of_umgolc2((void *)NULL, cx, NULL, t194, t195);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t9805qtonbsag t197 = t196;
                        if (!((((uint8_t *)&t197))[4] != 0)) goto ie193;
                        uint32_t t198 = GM_LD(uint32_t, ((uint8_t *)&t197));
                        gm_l24 = t198;
                        {
                            void * t200 = gm_l0;
                            void * t201 = gm_l13;
                            gm_tl874t2atnlh3 t202 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t201) + 120));
                            uint32_t t203 = gm_l24;
                            gm_tl874t2atnlh3 t204;
                            GM_ST(uint32_t, ((uint8_t *)&t204 + 0), t203);
                            GM_ST(uint32_t, ((uint8_t *)&t204 + 4), ((uint32_t)0ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t204 + 8), ((uint32_t)0ULL));
                            gm_b0_1 t205 = gm_f_add_ref_tgbe5vc((void *)NULL, cx, NULL, t200, t202, t204);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto ix193;
ie193:;
ix193:;
                    }
                    goto ix188;
ie188:;
ix188:;
                }
                goto ix180;
ie180:;
ix180:;
            }
            goto ix177;
ie177:;
ix177:;
        }
        goto me77;
mn170:;
ma81:;
        uint8_t t207 = GM_LD(uint8_t, ((((uint8_t *)t76) + 8) + 0));
        if (t207 != ((uint8_t)39ULL)) goto mn206;
        void * t208 = GM_LD(void *, ((((uint8_t *)t76) + 8) + 8));
        gm_l25 = t208;
        {
            gm_t9qkh0rsooc8d t210;
            GM_ST(uint8_t, ((uint8_t *)&t210 + 0), ((uint8_t)0ULL));
            gm_l8 = t210;
        }
        {
            void * t211 = gm_l0;
            void * t212 = gm_l2;
            void * t213 = GM_LD(void *, (((uint8_t *)t212) + 40));
            gm_b0_1 t214 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t211, t213);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l27 = ((uint64_t)0ULL);
            }
            {
                void * t216 = gm_l25;
                uint64_t t217 = GM_LD(uintptr_t, (((uint8_t *)t216) + 24) + 8);
                gm_l26 = t217;
            }
            {
                for (;;) {
                    uint64_t t219 = gm_l27;
                    uint64_t t220 = gm_l26;
                    uint8_t t221 = (t219 < t220);
                    if (!t221) goto lb218;
                    {
                        uint64_t t224 = gm_l27;
                        uint64_t t225 = t224;
                        uint64_t t228 = GM_LD(uint64_t, ((uint8_t *)&t225));
                        gm_l28 = t228;
                        {
                            uint64_t t230 = gm_l28;
                            uint64_t t231 = (uintptr_t)t230;
                            void * t232 = gm_l25;
                            uint64_t t233 = GM_LD(uintptr_t, (((uint8_t *)t232) + 24) + 8);
                            if (t231 >= (uintptr_t)(t233)) gm_trap_index(t231, t233, gm_src94heo671ss5e, 304, 23);
                            void * t234 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t232) + 24)) + t231 * 144);
                            gm_l29 = t234;
                        }
                        {
                            void * t236 = gm_l29;
                            uint8_t t237 = GM_LD(uint8_t, ((((uint8_t *)t236) + 16) + 0));
                            if (t237 != ((uint8_t)0ULL)) goto ie235;
                            gm_t08olq97rscn3 t238 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t236) + 16) + 8) + 0));
                            gm_l30 = t238;
                            gm_tlivgedjs5ps3 t239 = GM_LD(gm_tlivgedjs5ps3, (((((uint8_t *)t236) + 16) + 8) + 32));
                            F.l31 = t239;
                            {
                                void * t241 = (void *)((uint8_t *)&F.l12);
                                gm_t80lhgi01aeei t242;
                                memset((uint8_t *)&t242 + 17, 0, 7);
                                gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t242 + 0, t243);
                                gm_t9qkh0rsooc8d t244;
                                GM_ST(uint8_t, ((uint8_t *)&t244 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t242 + 16, t244);
                                void * t245 = gm_l0;
                                void * t246 = GM_LD(void *, (((uint8_t *)t245) + 0));
                                gm_tl874t2atnlh3 t247 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l31) + 56));
                                gm_b16_8 t248 = gm_f_src_fjcc76p(t246, GM_CONV(gm_cseahvg8v7p0vs, t247));
                                GM_ST(gm_b16_8, (uint8_t *)&t242 + 24, t248);
                                void * t249 = gm_l29;
                                gm_tl874t2atnlh3 t250 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t249) + 128));
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t242 + 40, t250);
                                gm_tl874t2atnlh3 t251 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l30) + 16));
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t242 + 52, t251);
                                gm_b16_8 t252 = gm_f_empty_slice_6ld1tun();
                                GM_ST(gm_b16_8, (uint8_t *)&t242 + 64, t252);
                                gm_tftb1odc739t5 t253 = gm_f_push_le0hqcb(t241, &t242);
                                gm_tftb1odc739t5 t254 = t253;
                                if (((((uint8_t *)&t254))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t254) + 1, gm_src94heo671ss5e, 306, 21);
                                gm_b0_1 t255 = GM_LD(gm_b0_1, (((uint8_t *)&t254) + 1));
                            }
                            goto ix235;
ie235:;
ix235:;
                        }
                        goto me226;
mn227:;
                        gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 303, 13);
me226:;
                    }
lc218:;
                    {
                        uint64_t t257 = gm_l27;
                        uint64_t r258; bool o259 = __builtin_add_overflow(t257, ((uint64_t)1ULL), &r258);
                        uint64_t t260 = r258;
                        gm_l27 = t260;
                    }
                }
lb218:;
            }
        }
        goto me77;
mn206:;
ma82:;
        void * t262 = gm_l0;
        void * t263 = gm_l2;
        void * t264 = GM_LD(void *, (((uint8_t *)t263) + 40));
        gm_b0_1 t265 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t262, t264);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me77;
mn261:;
ma83:;
        gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 265, 5);
me77:;
    }
    {
        void * t266 = gm_l3;
        gm_t80lhgi01aeei t267;
        memset((uint8_t *)&t267 + 17, 0, 7);
        gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t267 + 0, t268);
        gm_t9qkh0rsooc8d t269 = gm_l8;
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t267 + 16, t269);
        gm_b16_8 t270 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t267 + 24, t270);
        void * t271 = gm_l1;
        gm_tl874t2atnlh3 t272 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t271) + 4));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t267 + 40, t272);
        gm_tl874t2atnlh3 t273 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 16));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t267 + 52, t273);
        gm_b16_8 t274 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t267 + 64, t274);
        gm_tftb1odc739t5 t275 = gm_f_push_le0hqcb(t266, &t267);
        gm_tftb1odc739t5 t276 = t275;
        if (((((uint8_t *)&t276))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t276) + 1, gm_src94heo671ss5e, 319, 5);
        gm_b0_1 t277 = GM_LD(gm_b0_1, (((uint8_t *)&t276) + 1));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_type_lit_lm0h7k6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_type_lit_lm0h7k6 F;
    void * gm_l0; /* w */
    void * gm_l1; /* e */
    void * gm_l2; /* out */
    void * gm_l3; /* lit */
    gm_b16_8 gm_l4; /* ps */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    void * gm_l8; /* lit */
    gm_b16_8 gm_l9; /* ps */
    void * gm_l10; /* t */
    void * gm_l11; /* lit */
    gm_b16_8 gm_l12; /* ps */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    void * gm_l16; /* op */
    void * gm_l17; /* r */
    gm_b16_8 gm_l18; /* detail */
    void * gm_l19; /* inner */
    GM_STACK_CHECK(&F, 1424u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma4; case ((uint8_t)33ULL): goto ma5; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)32ULL)) goto mn11;
        void * t13 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t13;
        {
            void * t15 = gm_l0;
            void * t16 = gm_l3;
            uint32_t t17 = GM_LD(uint32_t, ((((uint8_t *)t16) + 56) + 8));
            gm_b0_1 t18 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t15, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t20 = gm_l3;
            if (!(((((uint8_t *)t20) + 0))[16] != 0)) goto ie19;
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t20) + 0));
            gm_l4 = t21;
            {
                void * t23 = gm_l0;
                gm_b16_8 t24 = gm_l4;
                gm_b16_8 t25 = t24;
                gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
                gm_b0_1 t27 = gm_f_params_ufsghpd((void *)NULL, cx, NULL, t23, t26, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix19;
ie19:;
ix19:;
        }
        {
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                void * t29 = gm_l3;
                uint64_t t30 = GM_LD(uintptr_t, (((uint8_t *)t29) + 24) + 8);
                gm_l5 = t30;
            }
            {
                for (;;) {
                    uint64_t t32 = gm_l6;
                    uint64_t t33 = gm_l5;
                    uint8_t t34 = (t32 < t33);
                    if (!t34) goto lb31;
                    {
                        uint64_t t37 = gm_l6;
                        uint64_t t38 = t37;
                        uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&t38));
                        gm_l7 = t41;
                        {
                            void * t43 = gm_l0;
                            uint64_t t44 = gm_l7;
                            uint64_t t45 = (uintptr_t)t44;
                            void * t46 = gm_l3;
                            uint64_t t47 = GM_LD(uintptr_t, (((uint8_t *)t46) + 24) + 8);
                            if (t45 >= (uintptr_t)(t47)) gm_trap_index(t45, t47, gm_src94heo671ss5e, 97, 49);
                            void * t48 = GM_LD(void *, (((uint8_t *)GM_LD(void *, (((uint8_t *)t46) + 24)) + t45 * 56) + 32));
                            gm_b0_1 t49 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t43, t48);
                            if (gm_unw.kind) {
                                goto gm_ll35;
                            }
                        }
                        goto me39;
mn40:;
                        gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 97, 13);
me39:;
                    }
lc31:;
                    {
                        uint64_t t51 = gm_l6;
                        uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                        uint64_t t54 = r52;
                        gm_l6 = t54;
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
            void * t55 = gm_l0;
            void * t56 = gm_l3;
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)t56) + 40));
            gm_b16_8 t58 = t57;
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
            void * t60 = gm_l2;
            gm_b0_1 t61 = gm_f_struct_members_0n9oe2e((void *)NULL, cx, NULL, t55, t59, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t62 = gm_l0;
            gm_f_pop_scope_vgka5ae(t62);
        }
        goto me3;
mn11:;
ma5:;
        uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t64 != ((uint8_t)33ULL)) goto mn63;
        void * t65 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l8 = t65;
        {
            void * t67 = gm_l0;
            void * t68 = gm_l8;
            uint32_t t69 = GM_LD(uint32_t, ((((uint8_t *)t68) + 64) + 8));
            gm_b0_1 t70 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t67, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t72 = gm_l8;
            if (!(((((uint8_t *)t72) + 8))[16] != 0)) goto ie71;
            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)t72) + 8));
            gm_l9 = t73;
            {
                void * t75 = gm_l0;
                gm_b16_8 t76 = gm_l9;
                gm_b16_8 t77 = t76;
                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                gm_b0_1 t79 = gm_f_params_ufsghpd((void *)NULL, cx, NULL, t75, t78, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix71;
ie71:;
ix71:;
        }
        {
            void * t81 = gm_l8;
            void * t82 = GM_LD(void *, (((uint8_t *)t81) + 0));
            void * t83 = t82;
            if (!(GM_LD(void *, ((uint8_t *)&t83)) != 0)) goto ie80;
            void * t84 = GM_LD(void *, ((uint8_t *)&t83));
            gm_l10 = t84;
            {
                void * t86 = gm_l0;
                void * t87 = gm_l10;
                gm_b0_1 t88 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t86, t87);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix80;
ie80:;
ix80:;
        }
        {
            void * t89 = gm_l0;
            void * t90 = gm_l8;
            gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)t90) + 48));
            gm_b16_8 t92 = t91;
            gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
            void * t94 = gm_l2;
            gm_b0_1 t95 = gm_f_union_members_sg9s47f((void *)NULL, cx, NULL, t89, t93, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t96 = gm_l0;
            gm_f_pop_scope_vgka5ae(t96);
        }
        goto me3;
mn63:;
ma6:;
        uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t98 != ((uint8_t)34ULL)) goto mn97;
        void * t99 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l11 = t99;
        {
            void * t101 = gm_l0;
            void * t102 = gm_l11;
            uint32_t t103 = GM_LD(uint32_t, ((((uint8_t *)t102) + 40) + 8));
            gm_b0_1 t104 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t101, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t106 = gm_l11;
            if (!(((((uint8_t *)t106) + 0))[16] != 0)) goto ie105;
            gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)t106) + 0));
            gm_l12 = t107;
            {
                void * t109 = gm_l0;
                gm_b16_8 t110 = gm_l12;
                gm_b16_8 t111 = t110;
                gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                gm_b0_1 t113 = gm_f_params_ufsghpd((void *)NULL, cx, NULL, t109, t112, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix105;
ie105:;
ix105:;
        }
        {
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                void * t115 = gm_l11;
                uint64_t t116 = GM_LD(uintptr_t, (((uint8_t *)t115) + 24) + 8);
                gm_l13 = t116;
            }
            {
                for (;;) {
                    uint64_t t118 = gm_l14;
                    uint64_t t119 = gm_l13;
                    uint8_t t120 = (t118 < t119);
                    if (!t120) goto lb117;
                    {
                        uint64_t t123 = gm_l14;
                        uint64_t t124 = t123;
                        uint64_t t127 = GM_LD(uint64_t, ((uint8_t *)&t124));
                        gm_l15 = t127;
                        {
                            uint64_t t129 = gm_l15;
                            uint64_t t130 = (uintptr_t)t129;
                            void * t131 = gm_l11;
                            uint64_t t132 = GM_LD(uintptr_t, (((uint8_t *)t131) + 24) + 8);
                            if (t130 >= (uintptr_t)(t132)) gm_trap_index(t130, t132, gm_src94heo671ss5e, 112, 24);
                            void * t133 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t131) + 24)) + t130 * 80);
                            gm_l16 = t133;
                        }
                        {
                            void * t134 = gm_l0;
                            void * t135 = gm_l16;
                            uint32_t t136 = GM_LD(uint32_t, ((((uint8_t *)t135) + 64) + 8));
                            gm_b0_1 t137 = gm_f_push_scope_u4jfguj((void *)NULL, cx, NULL, t134, t136);
                            if (gm_unw.kind) {
                                goto gm_ll121;
                            }
                        }
                        {
                            void * t138 = gm_l0;
                            void * t139 = gm_l16;
                            gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)t139) + 40));
                            gm_b16_8 t141 = t140;
                            gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
                            gm_b0_1 t143 = gm_f_params_ufsghpd((void *)NULL, cx, NULL, t138, t142, ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_ll121;
                            }
                        }
                        {
                            void * t145 = gm_l16;
                            void * t146 = GM_LD(void *, (((uint8_t *)t145) + 56));
                            void * t147 = t146;
                            if (!(GM_LD(void *, ((uint8_t *)&t147)) != 0)) goto ie144;
                            void * t148 = GM_LD(void *, ((uint8_t *)&t147));
                            gm_l17 = t148;
                            {
                                void * t150 = gm_l0;
                                void * t151 = gm_l17;
                                gm_b0_1 t152 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t150, t151);
                                if (gm_unw.kind) {
                                    goto gm_ll121;
                                }
                            }
                            goto ix144;
ie144:;
ix144:;
                        }
                        {
                            void * t153 = gm_l0;
                            gm_f_pop_scope_vgka5ae(t153);
                        }
                        {
                            void * t154 = gm_l0;
                            void * t155 = GM_LD(void *, (((uint8_t *)t154) + 0));
                            void * t156 = gm_l16;
                            gm_tl874t2atnlh3 t157 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t156) + 64));
                            gm_b16_8 t158 = gm_f_src_fjcc76p(t155, GM_CONV(gm_cseahvg8v7p0vs, t157));
                            gm_b16_8 t159 = gm_f_trim_fdrfe6i(t158);
                            gm_l18 = t159;
                        }
                        {
                            void * t160 = gm_l2;
                            gm_t80lhgi01aeei t161;
                            memset((uint8_t *)&t161 + 17, 0, 7);
                            void * t162 = gm_l16;
                            gm_b16_8 t163 = GM_LD(gm_b16_8, ((((uint8_t *)t162) + 0) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t161 + 0, t163);
                            gm_t9qkh0rsooc8d t164;
                            GM_ST(uint8_t, ((uint8_t *)&t164 + 0), ((uint8_t)12ULL));
                            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t161 + 16, t164);
                            gm_b16_8 t165 = gm_l18;
                            GM_ST(gm_b16_8, (uint8_t *)&t161 + 24, t165);
                            void * t166 = gm_l16;
                            gm_tl874t2atnlh3 t167 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t166) + 64));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t161 + 40, t167);
                            void * t168 = gm_l16;
                            gm_tl874t2atnlh3 t169 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t168) + 0) + 16));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t161 + 52, t169);
                            gm_b16_8 t170 = gm_f_empty_slice_6ld1tun();
                            GM_ST(gm_b16_8, (uint8_t *)&t161 + 64, t170);
                            gm_tftb1odc739t5 t171 = gm_f_push_le0hqcb(t160, &t161);
                            gm_tftb1odc739t5 t172 = t171;
                            if (((((uint8_t *)&t172))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t172) + 1, gm_src94heo671ss5e, 118, 17);
                            gm_b0_1 t173 = GM_LD(gm_b0_1, (((uint8_t *)&t172) + 1));
                        }
                        goto me125;
mn126:;
                        gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 111, 13);
me125:;
                    }
lc117:;
                    {
                        uint64_t t175 = gm_l14;
                        uint64_t r176; bool o177 = __builtin_add_overflow(t175, ((uint64_t)1ULL), &r176);
                        uint64_t t178 = r176;
                        gm_l14 = t178;
                    }
                }
gm_ll121:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb117; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc117; }
                }
                goto gm_lad;
lb117:;
            }
        }
        {
            void * t179 = gm_l0;
            gm_f_pop_scope_vgka5ae(t179);
        }
        goto me3;
mn97:;
ma7:;
        uint8_t t181 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t181 != ((uint8_t)35ULL)) goto mn180;
        void * t182 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l19 = t182;
        void * t183 = gm_l0;
        void * t184 = gm_l19;
        void * t185 = gm_l2;
        gm_b0_1 t186 = gm_f_type_lit_lm0h7k6((void *)NULL, cx, NULL, t183, t184, t185);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn180:;
ma8:;
        void * t188 = gm_l0;
        void * t189 = gm_l1;
        gm_b0_1 t190 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t188, t189);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn187:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 93, 5);
me3:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_type_literal_nnom1cv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_type_literal_nnom1cv F;
    void * gm_l0; /* w */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_uc1atd0((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        void * t4 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t5 = gm_f_type_lit_lm0h7k6((void *)NULL, cx, NULL, t2, t3, t4);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_union_members_sg9s47f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_union_members_sg9s47f F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* members */
    void * gm_l2; /* out */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    void * gm_l7; /* p */
    void * gm_l8; /* x */
    gm_b16_8 gm_l9; /* text */
    gm_b16_8 gm_l10; /* detail */
    void * gm_l11; /* p */
    gm_b16_8 gm_l13; /* e */
    GM_STACK_CHECK(&F, 1116u);
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
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l3 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = gm_l3;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = t10;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_src94heo671ss5e, 165, 15);
                        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t17 * 72) + 0));
                        if (t21 != ((uint8_t)0ULL)) goto mn20;
                        gm_tnljbma3s3h16 t22 = GM_LD(gm_tnljbma3s3h16, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t17 * 72) + 8));
                        F.l6 = t22;
                        {
                            void * t25 = GM_LD(void *, (((uint8_t *)&F.l6) + 32));
                            void * t26 = t25;
                            if (!(GM_LD(void *, ((uint8_t *)&t26)) != 0)) goto ie24;
                            void * t27 = GM_LD(void *, ((uint8_t *)&t26));
                            gm_l7 = t27;
                            {
                                void * t29 = gm_l0;
                                void * t30 = gm_l7;
                                gm_b0_1 t31 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t29, t30);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            goto ix24;
ie24:;
ix24:;
                        }
                        {
                            void * t33 = GM_LD(void *, (((uint8_t *)&F.l6) + 40));
                            void * t34 = t33;
                            if (!(GM_LD(void *, ((uint8_t *)&t34)) != 0)) goto ie32;
                            void * t35 = GM_LD(void *, ((uint8_t *)&t34));
                            gm_l8 = t35;
                            {
                                void * t37 = gm_l0;
                                void * t38 = gm_l8;
                                gm_b0_1 t39 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t37, t38);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            goto ix32;
ie32:;
ix32:;
                        }
                        {
                            void * t40 = gm_l0;
                            void * t41 = GM_LD(void *, (((uint8_t *)t40) + 0));
                            gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 48));
                            gm_b16_8 t43 = gm_f_src_fjcc76p(t41, GM_CONV(gm_cseahvg8v7p0vs, t42));
                            gm_b16_8 t44 = gm_f_trim_fdrfe6i(t43);
                            gm_l9 = t44;
                        }
                        {
                            void * t45 = gm_l0;
                            gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l6) + 0) + 16));
                            gm_t6ok5tvcjc200 t47;
                            gm_b16_8 t48 = gm_l9;
                            GM_ST(gm_b16_8, (uint8_t *)&t47 + 0, t48);
                            gm_th2lqbo77pvsc t49;
                            memset(&t49, 0, sizeof t49);
                            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t47 + 16, t49);
                            gm_b0_1 t50 = gm_f_def_once_pqgvduh((void *)NULL, cx, NULL, t45, t46, &t47);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            gm_b16_8 t51;
                            GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(0));
                            gm_l10 = t51;
                        }
                        {
                            void * t53 = GM_LD(void *, (((uint8_t *)&F.l6) + 32));
                            void * t54 = t53;
                            if (!(GM_LD(void *, ((uint8_t *)&t54)) != 0)) goto ie52;
                            void * t55 = GM_LD(void *, ((uint8_t *)&t54));
                            gm_l11 = t55;
                            {
                                void * t57 = gm_l0;
                                void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
                                void * t59 = gm_l11;
                                gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t59) + 120));
                                gm_b16_8 t61 = gm_f_src_fjcc76p(t58, GM_CONV(gm_cseahvg8v7p0vs, t60));
                                gm_tnpifv20irnph t62;
                                GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t61);
                                gm_tabb710d9mq2e t63;
                                gm_f_format_407qqln((void *)&t63, (void *)NULL, cx, NULL, t62);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                                gm_tabb710d9mq2e t64 = t63;
                                if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 8, gm_src94heo671ss5e, 172, 46);
                                gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
                                gm_b16_8 t66 = t65;
                                gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
                                gm_l10 = t67;
                            }
                            goto ix52;
ie52:;
ix52:;
                        }
                        {
                            void * t68 = gm_l2;
                            gm_t80lhgi01aeei t69;
                            memset((uint8_t *)&t69 + 17, 0, 7);
                            gm_b16_8 t70 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 0) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t69 + 0, t70);
                            gm_t9qkh0rsooc8d t71;
                            GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)11ULL));
                            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t69 + 16, t71);
                            gm_b16_8 t72 = gm_l10;
                            GM_ST(gm_b16_8, (uint8_t *)&t69 + 24, t72);
                            gm_tl874t2atnlh3 t73 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 48));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t69 + 40, t73);
                            gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l6) + 0) + 16));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t69 + 52, t74);
                            gm_b16_8 t75 = gm_f_empty_slice_6ld1tun();
                            GM_ST(gm_b16_8, (uint8_t *)&t69 + 64, t75);
                            gm_tftb1odc739t5 t76 = gm_f_push_le0hqcb(t68, &t69);
                            gm_tftb1odc739t5 t77 = t76;
                            if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_src94heo671ss5e, 173, 17);
                            gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
                        }
                        goto me19;
mn20:;
                        uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t17 * 72) + 0));
                        if (t80 != ((uint8_t)1ULL)) goto mn79;
                        gm_t7bjbmukv26e7 t81 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t17 * 72) + 8));
                        F.l12 = t81;
                        {
                            void * t83 = gm_l0;
                            void * t84 = GM_LD(void *, (((uint8_t *)&F.l12) + 0));
                            gm_b0_1 t85 = gm_f_expr_kih3eo8((void *)NULL, cx, NULL, t83, t84);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t86 = gm_l0;
                            gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 8));
                            gm_b16_8 t88 = t87;
                            gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
                            void * t90 = gm_l2;
                            gm_b0_1 t91 = gm_f_union_members_sg9s47f((void *)NULL, cx, NULL, t86, t89, t90);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            if (!(((((uint8_t *)&F.l12) + 24))[16] != 0)) goto ie92;
                            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 24));
                            gm_l13 = t93;
                            {
                                void * t95 = gm_l0;
                                gm_b16_8 t96 = gm_l13;
                                gm_b16_8 t97 = t96;
                                gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
                                void * t99 = gm_l2;
                                gm_b0_1 t100 = gm_f_union_members_sg9s47f((void *)NULL, cx, NULL, t95, t98, t99);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            goto ix92;
ie92:;
ix92:;
                        }
                        goto me19;
mn79:;
                        gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 165, 9);
me19:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_src94heo671ss5e, 164, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t102 = gm_l4;
                    uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                    uint64_t t105 = r103;
                    gm_l4 = t105;
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
