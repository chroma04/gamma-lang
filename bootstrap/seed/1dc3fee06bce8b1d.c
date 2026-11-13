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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tcrdb1inj22os;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_b16_8 f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t p5[1]; uint32_t f5; } gm_th32vq80aukjr;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
typedef struct { gm_tiilmfd16v3fc f0; gm_b16_8 f1; } gm_t1cs2fjnrqap1;
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
typedef struct { uint64_t f0; gm_tsoie1ppgdvcg f1; } gm_t5im2v50m3hk6;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tcrdb1inj22os) == 72 && _Alignof(gm_tcrdb1inj22os) == 8 && offsetof(gm_tcrdb1inj22os, f0) == 0 && offsetof(gm_tcrdb1inj22os, f1) == 16 && offsetof(gm_tcrdb1inj22os, f2) == 24 && offsetof(gm_tcrdb1inj22os, f3) == 40 && offsetof(gm_tcrdb1inj22os, f4) == 56, "layout of `(string, usize, string, string, string)`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th32vq80aukjr) == 32 && _Alignof(gm_th32vq80aukjr) == 8 && offsetof(gm_th32vq80aukjr, f0) == 0 && offsetof(gm_th32vq80aukjr, f1) == 8 && offsetof(gm_th32vq80aukjr, f2) == 24 && offsetof(gm_th32vq80aukjr, f3) == 25 && offsetof(gm_th32vq80aukjr, f4) == 26 && offsetof(gm_th32vq80aukjr, f5) == 28, "layout of `(u8, string, bool, bool, bool, u32)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_trqfvmjtt4sij f1; uint32_t f2; uint8_t pe[4]; } gm_tk0navpiff9ca;
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
typedef struct { gm_tiilmfd16v3fc f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; gm_b16_8 f3; } gm_tvc83pqnepc2e;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1cs2fjnrqap1) == 40 && _Alignof(gm_t1cs2fjnrqap1) == 8 && offsetof(gm_t1cs2fjnrqap1, f0) == 0 && offsetof(gm_t1cs2fjnrqap1, f1) == 24, "layout of `HandlerLit`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
_Static_assert(sizeof(gm_t5im2v50m3hk6) == 48 && _Alignof(gm_t5im2v50m3hk6) == 8 && offsetof(gm_t5im2v50m3hk6, f0) == 0 && offsetof(gm_t5im2v50m3hk6, f1) == 8, "layout of `(i: usize, x: LocalDecl)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
_Static_assert(sizeof(gm_tk0navpiff9ca) == 56 && _Alignof(gm_tk0navpiff9ca) == 8 && offsetof(gm_tk0navpiff9ca, f0) == 0 && offsetof(gm_tk0navpiff9ca, f1) == 8 && offsetof(gm_tk0navpiff9ca, f2) == 48, "layout of `Dump`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tvc83pqnepc2e) == 64 && _Alignof(gm_tvc83pqnepc2e) == 8 && offsetof(gm_tvc83pqnepc2e, f0) == 0 && offsetof(gm_tvc83pqnepc2e, f1) == 24 && offsetof(gm_tvc83pqnepc2e, f2) == 32 && offsetof(gm_tvc83pqnepc2e, f3) == 48, "layout of `HandlerDeclInfo`");
typedef struct { gm_th1g69d7vpipg f0; void * f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_t9i38ih07bojq;
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_t9i38ih07bojq) == 64 && _Alignof(gm_t9i38ih07bojq) == 8 && offsetof(gm_t9i38ih07bojq, f0) == 0 && offsetof(gm_t9i38ih07bojq, f1) == 40 && offsetof(gm_t9i38ih07bojq, f2) == 48 && offsetof(gm_t9i38ih07bojq, f3) == 56, "layout of `WriteDeferredExpr`");
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
struct gm_fr_binop_text_t4cv0b4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_builtin_text_od38u7h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_coercion_text_g32oppe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_escaped_k5fm4jl {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_flavor_text_3ffnbt7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_label_9hbn37h {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* args */
    gm_b16_8 ret;
};
struct gm_fr_fn_pfpd9oj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_local_text_75tuacb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_place_lj97s4p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_rmw_text_te4ol3u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_sink_text_s4o8tr0 {
    gm_frame h;
    gm_th1g69d7vpipg l1; /* sink */
    gm_b16_8 ret;
};
struct gm_fr_soa_op_text_ud90n2l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_spec_text_fr9884d {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sys_text_9qjnpra {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_target_text_4kebb7t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_tir_dump_ppa6e9g {
    gm_frame h;
    void * l0; /* s */
    gm_trqfvmjtt4sij l1; /* order */
    gm_tk0navpiff9ca l6; /* d */
    gm_b16_8 ret;
};
struct gm_fr_write_arm_4vimkqo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_block_q5mpqjf {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* names */
    gm_b0_1 ret;
};
struct gm_fr_write_conds_k9e57un {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_expr_cftucor {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* kids */
    gm_tbci5ospk5nnn l6; /* v */
    gm_tngtuiuskn938 l8; /* r */
    gm_tu15p0hi2qlv6 l13; /* c */
    gm_t1cs2fjnrqap1 l48; /* lit */
    gm_trqfvmjtt4sij l49; /* labels */
    gm_t710lsrq1m8gk l53; /* w */
    gm_tsth9qu1dv0g3 l69; /* callee */
    gm_t710lsrq1m8gk l110; /* x */
    gm_tntn2tf15puib l120; /* w */
    gm_t9i38ih07bojq l125; /* w */
    gm_b0_1 ret;
};
struct gm_fr_write_fn_jkakt7l {
    gm_frame h;
    gm_tsoie1ppgdvcg l11; /* l */
    gm_trqfvmjtt4sij l13; /* names */
    gm_b0_1 ret;
};
struct gm_fr_write_if_oqjiok5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_line_d6lkps7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_loop_u0ja07u {
    gm_frame h;
    gm_t710lsrq1m8gk l2; /* x */
    gm_b0_1 ret;
};
struct gm_fr_write_pat_4qu9dcc {
    gm_frame h;
    gm_tbci5ospk5nnn l6; /* lo */
    gm_tbci5ospk5nnn l7; /* hi */
    gm_b0_1 ret;
};
struct gm_fr_write_text_h6e1ibp {
    gm_frame h;
    gm_tntn2tf15puib l1; /* w */
    gm_trqfvmjtt4sij l2; /* h */
    gm_b16_8 ret;
};
struct gm_fr_write_with_j37nil7 {
    gm_frame h;
    gm_t710lsrq1m8gk l2; /* w */
    gm_trqfvmjtt4sij l11; /* names */
    gm_tvc83pqnepc2e l15; /* hd */
    gm_b0_1 ret;
};
bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_t4cv0b4(void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_builtin_text_od38u7h(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_coercion_text_g32oppe(void *env, gm_node *cx, gm_node *inst, gm_tmn7n0n9raou5 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_bytes_25mo226(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_escaped_k5fm4jl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_flavor_text_3ffnbt7(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_label_9hbn37h(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_fn_pfpd9oj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_02b5heb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_0969kd8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0gd6afr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
__attribute__((aligned(4))) void gm_f_format_0n7d8jn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_16dq22e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
__attribute__((aligned(4))) void gm_f_format_1d88j1c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_208te7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_2lgoflc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_32bp3b4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3669u6h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_3aao56v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_41c1370(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_45lt1qt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4i3nb5n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_toh0ootodhjue a0);
__attribute__((aligned(4))) void gm_f_format_556kaa4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5m6a3eh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5up8eqa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6b7mjf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_6h8qssq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6na8imi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_7316bju(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7ejl94l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_85biid9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8k3m4s1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8vmpj9s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9g7fct9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_9nv39ua(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_a4vi4f7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_aq84o6u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_arnesr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_as0b1t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_booci4l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_c2ann39(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
__attribute__((aligned(4))) void gm_f_format_c726p1u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_cqr3tc0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_cuuc21t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ejli3qf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fi775k0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fp763nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_galn15q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_glqqt6c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_gnbipfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_i37o6qg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_i4fsdjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_i7k9v76(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_irh7ehn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ka8hqpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kj3qi5h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
__attribute__((aligned(4))) void gm_f_format_lde77u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_lderfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lefnhqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_licpkoa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_llf38gp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_lpj4rro(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nva8q4r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_oc8ipo6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_olqcc5i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_p2bh4bb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_p7ogfqk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_pcmnhpk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_pvddll3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qcp7nf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qhad3qp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r0u4lpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_r8pfaos(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r9ba7g9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ree2v5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_rvu96av(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_sqbp092(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_sugh2p3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t0vntpb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t725p24(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vmi8kfe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vprps7i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_41b084m(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_local_text_75tuacb(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_place_lj97s4p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_rmw_text_te4ol3u(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_rat_1s624db(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sink_text_s4o8tr0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_op_text_ud90n2l(void *env, gm_node *cx, gm_node *inst, gm_tesaltf0mhcki a0);
__attribute__((aligned(4))) void gm_f_sort_by_egbtonb(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_spec_text_fr9884d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_text_9qjnpra(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_target_text_4kebb7t(void *env, gm_node *cx, gm_node *inst, gm_te9hrk0u35rev a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_tir_dump_ppa6e9g(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7h6pth0(void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7ilko9q(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_arm_4vimkqo(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_block_q5mpqjf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_conds_k9e57un(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_expr_cftucor(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_fn_jkakt7l(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_if_oqjiok5(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_line_d6lkps7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_loop_u0ja07u(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_pat_4qu9dcc(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_t5i5r03(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_write_text_h6e1ibp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uebhmoa(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_with_j37nil7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s00pihipe0jga[6];
extern const uint8_t gm_s026u6q948evn[5];
extern const uint8_t gm_s0eqrtq5mqb3a[16];
extern const uint8_t gm_s0kvnv9teor3q[4];
extern const uint8_t gm_s0lh54534p3vd[10];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0ve0dba4rtj8[13];
extern const uint8_t gm_s187rb18i1kgk[4];
extern const uint8_t gm_s19069udlvhni[9];
extern const uint8_t gm_s19ndlkgdtviu[6];
extern const uint8_t gm_s1gf327eapo1j[11];
extern const uint8_t gm_s1j5448m6ruti[10];
extern const uint8_t gm_s1js4dn5e65ja[11];
extern const uint8_t gm_s1n20vngt0mkv[3];
extern const uint8_t gm_s1q2nkavemrqf[9];
extern const uint8_t gm_s1q9ms0kviddj[4];
extern const uint8_t gm_s2289h15tkpi1[12];
extern const uint8_t gm_s24chpb914bep[6];
extern const uint8_t gm_s28ijm46j20ds[8];
extern const uint8_t gm_s2a9t8smkevg0[4];
extern const uint8_t gm_s2fdsodctojm1[10];
extern const uint8_t gm_s2fu5nkek0lrq[7];
extern const uint8_t gm_s2h8cr50573go[8];
extern const uint8_t gm_s2iprgandb78l[8];
extern const uint8_t gm_s2kmbpuutp9qq[5];
extern const uint8_t gm_s2ma5cp41jk1a[7];
extern const uint8_t gm_s2nl2e5anglov[4];
extern const uint8_t gm_s2uphqpvb2vhh[4];
extern const uint8_t gm_s2uqljf8sumcn[11];
extern const uint8_t gm_s35gfshei80n5[8];
extern const uint8_t gm_s3ng5nguvd452[5];
extern const uint8_t gm_s3o5gaeteeggu[5];
extern const uint8_t gm_s3ol5q9br79vf[8];
extern const uint8_t gm_s3opc5ivp6e66[4];
extern const uint8_t gm_s3s50a53ci7rk[7];
extern const uint8_t gm_s49o1oiv7k0po[4];
extern const uint8_t gm_s4bdmssec9aiu[4];
extern const uint8_t gm_s4g3rq58567m6[8];
extern const uint8_t gm_s4gilalcmdvu3[8];
extern const uint8_t gm_s4lo69slu4igt[8];
extern const uint8_t gm_s4lt93qhapike[4];
extern const uint8_t gm_s55ccd7do7rbs[11];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s57rgvjb5mj5a[8];
extern const uint8_t gm_s5c5548ll8taj[10];
extern const uint8_t gm_s5dbqnns2epkg[6];
extern const uint8_t gm_s5e0seq0he7ou[10];
extern const uint8_t gm_s5k0vuc678fh1[8];
extern const uint8_t gm_s5n5e484glelv[5];
extern const uint8_t gm_s5udr3eget84q[6];
extern const uint8_t gm_s6aamtn65omr1[7];
extern const uint8_t gm_s6ag7edtaqqri[5];
extern const uint8_t gm_s6edba3m7mc9a[6];
extern const uint8_t gm_s6ipt60dh9o1j[11];
extern const uint8_t gm_s6nm27ehg597q[7];
extern const uint8_t gm_s6ofccoi3ursa[12];
extern const uint8_t gm_s6p9ub6gu603m[7];
extern const uint8_t gm_s6qbnfe2f321h[4];
extern const uint8_t gm_s76ekntop342o[8];
extern const uint8_t gm_s76ud0alkjfse[4];
extern const uint8_t gm_s7d4tu5fkkg12[11];
extern const uint8_t gm_s7dkbj8tvt9e3[7];
extern const uint8_t gm_s7i80akiq0d95[5];
extern const uint8_t gm_s7m3rbk4l87ah[15];
extern const uint8_t gm_s7rltjaeh634d[11];
extern const uint8_t gm_s7u6ij52clodh[7];
extern const uint8_t gm_s85hqf14lbkns[8];
extern const uint8_t gm_s85jtgs7au5b5[9];
extern const uint8_t gm_s8gtcifve79ai[9];
extern const uint8_t gm_s8pk5tcnrk6p2[8];
extern const uint8_t gm_s8s6shfnaqahd[7];
extern const uint8_t gm_s920dpvvv6cej[4];
extern const uint8_t gm_s93umj3p0tala[5];
extern const uint8_t gm_s96j6o9smoqdu[8];
extern const uint8_t gm_s9dgf2prvg715[6];
extern const uint8_t gm_s9es2tos1bhfp[5];
extern const uint8_t gm_s9h93c7asacna[7];
extern const uint8_t gm_s9i2ld3a42lap[8];
extern const uint8_t gm_s9o8gb20t6jtp[9];
extern const uint8_t gm_s9v13eck7gubi[4];
extern const uint8_t gm_sa0alba1kin3k[8];
extern const uint8_t gm_sa1pcp92k3aj3[7];
extern const uint8_t gm_sa53epbg9iarq[10];
extern const uint8_t gm_sa5s0jools6a2[9];
extern const uint8_t gm_sa8dse4jk5ibv[10];
extern const uint8_t gm_sa902c1tbtg0i[4];
extern const uint8_t gm_sacua9qc75rts[7];
extern const uint8_t gm_sah1jmr01nidj[5];
extern const uint8_t gm_sai3vjkob4s2v[10];
extern const uint8_t gm_sajbccb18c45d[6];
extern const uint8_t gm_sakmuvnjjp217[10];
extern const uint8_t gm_sbckpj1jsdt28[13];
extern const uint8_t gm_sbcq15b9em6fm[9];
extern const uint8_t gm_sbek54n4a6h2o[7];
extern const uint8_t gm_sbgepjj6hgjej[4];
extern const uint8_t gm_sbnms71r2jqr5[7];
extern const uint8_t gm_sbqq5iijjahf9[5];
extern const uint8_t gm_sbsb3nmbfgbv2[4];
extern const uint8_t gm_sbuhie0vb9t62[9];
extern const uint8_t gm_sc03pp1oj3q4k[7];
extern const uint8_t gm_sc24ftfhihd1h[4];
extern const uint8_t gm_sc5qopgd7dm66[8];
extern const uint8_t gm_scb853lmgr58k[6];
extern const uint8_t gm_scd599tsljaee[5];
extern const uint8_t gm_sdak257j1qgl8[6];
extern const uint8_t gm_sdft8ov6bb7s6[8];
extern const uint8_t gm_sdg7bdfivjinb[3];
extern const uint8_t gm_sdh1dnevn1j10[8];
extern const uint8_t gm_sdl8mgtvebfcs[3];
extern const uint8_t gm_sdq6l7oddjqfe[12];
extern const uint8_t gm_sds03accef1j8[6];
extern const uint8_t gm_se0a26478itki[7];
extern const uint8_t gm_se0ivu8cf46o8[12];
extern const uint8_t gm_se19nf1421e37[4];
extern const uint8_t gm_sedrsudco1u16[10];
extern const uint8_t gm_seeo26ergtcha[5];
extern const uint8_t gm_seg9l1usqbs3j[4];
extern const uint8_t gm_segr993uh2crm[11];
extern const uint8_t gm_sel2c30qvm74f[7];
extern const uint8_t gm_seodvmll3gsjn[4];
extern const uint8_t gm_sepu95c7o773o[6];
extern const uint8_t gm_ser8ip7vtbv6c[9];
extern const uint8_t gm_setcl7798ov89[4];
extern const uint8_t gm_sf5llhuhdlajp[9];
extern const uint8_t gm_sfch9mo7lqp8l[3];
extern const uint8_t gm_sfd0mn3sol25u[4];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfid9en449upv[11];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgankvlme129s[3];
extern const uint8_t gm_sgfufhs3psibd[4];
extern const uint8_t gm_sgk015tlfp2cu[5];
extern const uint8_t gm_sgm565ktbj9ua[12];
extern const uint8_t gm_sgo00f0mafbnd[4];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_sgtriqnjufjbf[10];
extern const uint8_t gm_sh11qqm8nqu68[8];
extern const uint8_t gm_sh4e4kqha06b3[7];
extern const uint8_t gm_sh4niqnmh73co[6];
extern const uint8_t gm_shdnhcpvptc1q[5];
extern const uint8_t gm_shkq4botdoed7[6];
extern const uint8_t gm_shohh3180gu26[7];
extern const uint8_t gm_shtuc6se4lilq[7];
extern const uint8_t gm_shvp36midgf58[10];
extern const uint8_t gm_shvpku7ssg8d7[11];
extern const uint8_t gm_si1m7q6ia0hto[7];
extern const uint8_t gm_si4k9tiofr8t2[7];
extern const uint8_t gm_si9h59pjqftat[5];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sij2r6jhpquej[12];
extern const uint8_t gm_sim8pu6stc37a[11];
extern const uint8_t gm_sinmpfqtbo9in[5];
extern const uint8_t gm_siqgrmkn8n4vn[8];
extern const uint8_t gm_sismsc98l6u4e[4];
extern const uint8_t gm_sit8u7uppaik9[7];
extern const uint8_t gm_sivsapeh482sm[8];
extern const uint8_t gm_sj1m58fkg956m[8];
extern const uint8_t gm_sj696i9uo1a9s[14];
extern const uint8_t gm_sjbhp9l7seful[7];
extern const uint8_t gm_sjbl17pjsjm48[5];
extern const uint8_t gm_sjdrosvte680q[12];
extern const uint8_t gm_sjegrp8u9r3n3[8];
extern const uint8_t gm_sjeopa747clpb[4];
extern const uint8_t gm_sjfcbh429vmot[9];
extern const uint8_t gm_sjg1ri23hm753[6];
extern const uint8_t gm_sjourjtj9e3to[9];
extern const uint8_t gm_sk2b59ovo43qg[6];
extern const uint8_t gm_sk5s72mtujop8[5];
extern const uint8_t gm_sk721elh8ouv2[11];
extern const uint8_t gm_sk88tp2337lf6[13];
extern const uint8_t gm_skapbb3maknat[5];
extern const uint8_t gm_skbgm82aqj3q0[6];
extern const uint8_t gm_skissj10nclhr[6];
extern const uint8_t gm_skl3b78lo1gln[5];
extern const uint8_t gm_sksprenpp79ih[11];
extern const uint8_t gm_sl37uvr16qsu8[11];
extern const uint8_t gm_slgrfk23llffe[13];
extern const uint8_t gm_sllcm1pqagh28[6];
extern const uint8_t gm_slmii6hjigrcr[7];
extern const uint8_t gm_sln5qir3tgi7j[10];
extern const uint8_t gm_slnhe1h36jmub[9];
extern const uint8_t gm_slov3heeajhqn[12];
extern const uint8_t gm_sluco9qbar70k[10];
extern const uint8_t gm_sm017vofcj044[12];
extern const uint8_t gm_sm0gs6tv176le[8];
extern const uint8_t gm_sm3dios51ece1[10];
extern const uint8_t gm_smgioupbmjajr[14];
extern const uint8_t gm_smmus0krp2svr[6];
extern const uint8_t gm_smpl91sh0k8v4[9];
extern const uint8_t gm_smtmd013g2edr[4];
extern const uint8_t gm_smuec3qfsdh82[10];
extern const uint8_t gm_sn581p3lbgrjg[9];
extern const uint8_t gm_sn6sqlran7h3r[7];
extern const uint8_t gm_snaq7hvenqsum[4];
extern const uint8_t gm_snbhl8dsqq3ln[7];
extern const uint8_t gm_sngdu4l8vh3l8[5];
extern const uint8_t gm_snq4uaiieftl8[6];
extern const uint8_t gm_snstimc3n04u0[12];
extern const uint8_t gm_so1u9tqjfpsmc[9];
extern const uint8_t gm_so2sjlodq81qn[13];
extern const uint8_t gm_so3oq96gr0k5r[10];
extern const uint8_t gm_so8fkrgrobcsk[9];
extern const uint8_t gm_sobcm12sogdng[5];
extern const uint8_t gm_soe4gvhg9ei97[4];
extern const uint8_t gm_soerl6rds8b9v[12];
extern const uint8_t gm_sofbtko7t8taa[5];
extern const uint8_t gm_solj59tk4ri3k[9];
extern const uint8_t gm_sovel03lmfl8u[9];
extern const uint8_t gm_sp2smaof0i4j2[10];
extern const uint8_t gm_sp4uhnkunj2cg[3];
extern const uint8_t gm_spi47lg4tr80j[3];
extern const uint8_t gm_spoh8ft9n8oqt[4];
extern const uint8_t gm_spr274eleu2lj[4];
extern const uint8_t gm_spskhsf5tnedj[10];
extern const uint8_t gm_sq0h7jjc6dtk5[4];
extern const uint8_t gm_sqa0b48i8mvk7[6];
extern const uint8_t gm_sqflguscq1uc5[6];
extern const uint8_t gm_sqfu5kl4vat80[9];
extern const uint8_t gm_sqhhm0h7b3asd[6];
extern const uint8_t gm_sqhkrul9t68sa[8];
extern const uint8_t gm_sqlk0h0kae56e[8];
extern const uint8_t gm_sqrv416075ito[3];
extern const uint8_t gm_squedv51820an[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr899mamktpvo[7];
extern const uint8_t gm_src5778gpfoh9[9];
extern const char gm_srco4ljquuk5d87[];
extern const uint8_t gm_srjd0sq8k3d7s[11];
extern const uint8_t gm_srmrjv19jgvks[11];
extern const uint8_t gm_srntlv7ksi1vk[16];
extern const uint8_t gm_sromjese8e416[14];
extern const uint8_t gm_srri8c6al1v1g[5];
extern const uint8_t gm_ss27a20sarlvg[7];
extern const uint8_t gm_ss308e7joejdo[13];
extern const uint8_t gm_ss6dev5u7tl99[12];
extern const uint8_t gm_ss869lnvmj3m8[6];
extern const uint8_t gm_sse9dgkusbtnf[5];
extern const uint8_t gm_ssgeom7733jik[5];
extern const uint8_t gm_ssgn9mppmu3vl[16];
extern const uint8_t gm_ssigpbgi88it3[4];
extern const uint8_t gm_ssjmv3618n779[10];
extern const uint8_t gm_sstrtuk2b4jdg[10];
extern const uint8_t gm_st0l9v6pp5i7b[14];
extern const uint8_t gm_st1pc9bsku00q[9];
extern const uint8_t gm_st7pul9bg6mqu[8];
extern const uint8_t gm_st87fk7ojs4l8[6];
extern const uint8_t gm_st8ki5f85lc2j[13];
extern const uint8_t gm_std59a9b6lpnn[10];
extern const uint8_t gm_stdefoi5sk91c[12];
extern const uint8_t gm_stehcnejvoo24[6];
extern const uint8_t gm_stft4dc50tdce[8];
extern const uint8_t gm_stgqtu1f7bco8[8];
extern const uint8_t gm_stidgejoe9ul2[11];
extern const uint8_t gm_stivl0smu8eq0[9];
extern const uint8_t gm_stn52kuiq0jk2[12];
extern const uint8_t gm_stou1l3qp844f[10];
extern const uint8_t gm_su3ahsl9p584f[9];
extern const uint8_t gm_sugq7dl55ocon[7];
extern const uint8_t gm_sukutl224cgkm[8];
extern const uint8_t gm_sun4qq3op5opf[7];
extern const uint8_t gm_supvu1ruutbov[9];
extern const uint8_t gm_sv0fbf330lmcd[8];
extern const uint8_t gm_sv3olmne7h13s[4];
extern const uint8_t gm_sv8lqdk5nsv8i[5];
extern const uint8_t gm_svbhcvlahd4n2[3];
extern const uint8_t gm_svqq3k9j6h1lu[8];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_t4cv0b4(void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0) {
    struct gm_fr_binop_text_t4cv0b4 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* f */
    gm_t9qkh0rsooc8d gm_l2; /* f */
    gm_t9qkh0rsooc8d gm_l3; /* f */
    gm_t9qkh0rsooc8d gm_l4; /* f */
    GM_STACK_CHECK(&F, 834u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; default: goto ma7; }
ma3:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t11 != ((uint8_t)0ULL)) goto mn10;
    gm_t9qkh0rsooc8d t12 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l1 = t12;
    gm_t9qkh0rsooc8d t13 = gm_l1;
    gm_b16_8 t14 = gm_f_flavor_text_3ffnbt7(t13);
    gm_tnpifv20irnph t15;
    GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
    gm_tabb710d9mq2e t16;
    gm_f_format_0n7d8jn((void *)&t16, (void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t17 = t16;
    if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_srco4ljquuk5d87, 414, 16);
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
    gm_b16_8 t19 = t18;
    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
    t1 = t20;
    goto me2;
mn10:;
ma4:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t22 != ((uint8_t)1ULL)) goto mn21;
    gm_t9qkh0rsooc8d t23 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l2 = t23;
    gm_t9qkh0rsooc8d t24 = gm_l2;
    gm_b16_8 t25 = gm_f_flavor_text_3ffnbt7(t24);
    gm_tnpifv20irnph t26;
    GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
    gm_tabb710d9mq2e t27;
    gm_f_format_lderfbr((void *)&t27, (void *)NULL, cx, NULL, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t28 = t27;
    if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_srco4ljquuk5d87, 415, 16);
    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
    gm_b16_8 t30 = t29;
    gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
    t1 = t31;
    goto me2;
mn21:;
ma5:;
    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t33 != ((uint8_t)2ULL)) goto mn32;
    gm_t9qkh0rsooc8d t34 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l3 = t34;
    gm_t9qkh0rsooc8d t35 = gm_l3;
    gm_b16_8 t36 = gm_f_flavor_text_3ffnbt7(t35);
    gm_tnpifv20irnph t37;
    GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
    gm_tabb710d9mq2e t38;
    gm_f_format_olqcc5i((void *)&t38, (void *)NULL, cx, NULL, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t39 = t38;
    if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_srco4ljquuk5d87, 416, 16);
    gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
    gm_b16_8 t41 = t40;
    gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
    t1 = t42;
    goto me2;
mn32:;
ma6:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t44 != ((uint8_t)3ULL)) goto mn43;
    gm_t9qkh0rsooc8d t45 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l4 = t45;
    gm_t9qkh0rsooc8d t46 = gm_l4;
    gm_b16_8 t47 = gm_f_flavor_text_3ffnbt7(t46);
    gm_tnpifv20irnph t48;
    GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
    gm_tabb710d9mq2e t49;
    gm_f_format_sqbp092((void *)&t49, (void *)NULL, cx, NULL, t48);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t50 = t49;
    if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_srco4ljquuk5d87, 417, 16);
    gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
    gm_b16_8 t52 = t51;
    gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
    t1 = t53;
    goto me2;
mn43:;
ma7:;
    gm_b16_8 t55;
    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t72) { case ((uint8_t)0ULL): goto ma57; case ((uint8_t)1ULL): goto ma58; case ((uint8_t)2ULL): goto ma59; case ((uint8_t)3ULL): goto ma60; case ((uint8_t)4ULL): goto ma61; case ((uint8_t)5ULL): goto ma62; case ((uint8_t)6ULL): goto ma63; case ((uint8_t)7ULL): goto ma64; case ((uint8_t)8ULL): goto ma65; case ((uint8_t)9ULL): goto ma66; case ((uint8_t)10ULL): goto ma67; case ((uint8_t)11ULL): goto ma68; case ((uint8_t)12ULL): goto ma69; case ((uint8_t)13ULL): goto ma70; default: goto ma71; }
ma57:;
    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t74 != ((uint8_t)0ULL)) goto mn73;
    gm_b16_8 t75;
    GM_ST(void *, (uint8_t *)&t75, (void *)(gm_spr274eleu2lj));
    GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(3));
    t55 = t75;
    goto me56;
mn73:;
ma58:;
    uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t77 != ((uint8_t)1ULL)) goto mn76;
    gm_b16_8 t78;
    GM_ST(void *, (uint8_t *)&t78, (void *)(gm_ssigpbgi88it3));
    GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(3));
    t55 = t78;
    goto me56;
mn76:;
ma59:;
    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t80 != ((uint8_t)2ULL)) goto mn79;
    gm_b16_8 t81;
    GM_ST(void *, (uint8_t *)&t81, (void *)(gm_spoh8ft9n8oqt));
    GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(3));
    t55 = t81;
    goto me56;
mn79:;
ma60:;
    uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t83 != ((uint8_t)3ULL)) goto mn82;
    gm_b16_8 t84;
    GM_ST(void *, (uint8_t *)&t84, (void *)(gm_smtmd013g2edr));
    GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(3));
    t55 = t84;
    goto me56;
mn82:;
ma61:;
    uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t86 != ((uint8_t)4ULL)) goto mn85;
    gm_b16_8 t87;
    GM_ST(void *, (uint8_t *)&t87, (void *)(gm_s2uphqpvb2vhh));
    GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(3));
    t55 = t87;
    goto me56;
mn85:;
ma62:;
    uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t89 != ((uint8_t)5ULL)) goto mn88;
    gm_b16_8 t90;
    GM_ST(void *, (uint8_t *)&t90, (void *)(gm_snaq7hvenqsum));
    GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(3));
    t55 = t90;
    goto me56;
mn88:;
ma63:;
    uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t92 != ((uint8_t)6ULL)) goto mn91;
    gm_b16_8 t93;
    GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sc24ftfhihd1h));
    GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(3));
    t55 = t93;
    goto me56;
mn91:;
ma64:;
    uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t95 != ((uint8_t)7ULL)) goto mn94;
    gm_b16_8 t96;
    GM_ST(void *, (uint8_t *)&t96, (void *)(gm_snbhl8dsqq3ln));
    GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(6));
    t55 = t96;
    goto me56;
mn94:;
ma65:;
    uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t98 != ((uint8_t)8ULL)) goto mn97;
    gm_b16_8 t99;
    GM_ST(void *, (uint8_t *)&t99, (void *)(gm_snq4uaiieftl8));
    GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(5));
    t55 = t99;
    goto me56;
mn97:;
ma66:;
    uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t101 != ((uint8_t)9ULL)) goto mn100;
    gm_b16_8 t102;
    GM_ST(void *, (uint8_t *)&t102, (void *)(gm_sel2c30qvm74f));
    GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(6));
    t55 = t102;
    goto me56;
mn100:;
ma67:;
    uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t104 != ((uint8_t)10ULL)) goto mn103;
    gm_b16_8 t105;
    GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sqrv416075ito));
    GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(2));
    t55 = t105;
    goto me56;
mn103:;
ma68:;
    uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t107 != ((uint8_t)11ULL)) goto mn106;
    gm_b16_8 t108;
    GM_ST(void *, (uint8_t *)&t108, (void *)(gm_s1n20vngt0mkv));
    GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(2));
    t55 = t108;
    goto me56;
mn106:;
ma69:;
    uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t110 != ((uint8_t)12ULL)) goto mn109;
    gm_b16_8 t111;
    GM_ST(void *, (uint8_t *)&t111, (void *)(gm_sp4uhnkunj2cg));
    GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(2));
    t55 = t111;
    goto me56;
mn109:;
ma70:;
    uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t113 != ((uint8_t)13ULL)) goto mn112;
    gm_b16_8 t114;
    GM_ST(void *, (uint8_t *)&t114, (void *)(gm_sdg7bdfivjinb));
    GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(2));
    t55 = t114;
    goto me56;
mn112:;
ma71:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 418, 10);
me56:;
    t1 = t55;
    goto me2;
mn54:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 413, 58);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_builtin_text_od38u7h(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0) {
    struct gm_fr_builtin_text_od38u7h F;
    gm_tvdg94bk7aa22 gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* f */
    gm_t9qkh0rsooc8d gm_l2; /* r */
    gm_t9qkh0rsooc8d gm_l3; /* s */
    uint32_t gm_l4; /* outputs */
    uint32_t gm_l5; /* inputs */
    uint32_t gm_l6; /* id */
    uint32_t gm_l7; /* id */
    GM_STACK_CHECK(&F, 2223u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma3; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma9; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma9; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma9; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma9; case ((uint8_t)45ULL): goto ma9; case ((uint8_t)46ULL): goto ma9; case ((uint8_t)47ULL): goto ma9; case ((uint8_t)48ULL): goto ma9; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma9; case ((uint8_t)51ULL): goto ma9; case ((uint8_t)52ULL): goto ma9; case ((uint8_t)53ULL): goto ma9; case ((uint8_t)54ULL): goto ma9; case ((uint8_t)55ULL): goto ma9; case ((uint8_t)56ULL): goto ma9; case ((uint8_t)57ULL): goto ma9; case ((uint8_t)58ULL): goto ma9; case ((uint8_t)59ULL): goto ma9; case ((uint8_t)60ULL): goto ma9; case ((uint8_t)61ULL): goto ma9; case ((uint8_t)62ULL): goto ma9; case ((uint8_t)63ULL): goto ma9; case ((uint8_t)64ULL): goto ma9; case ((uint8_t)65ULL): goto ma9; case ((uint8_t)66ULL): goto ma9; case ((uint8_t)67ULL): goto ma9; case ((uint8_t)68ULL): goto ma4; case ((uint8_t)69ULL): goto ma9; case ((uint8_t)70ULL): goto ma9; case ((uint8_t)71ULL): goto ma9; case ((uint8_t)72ULL): goto ma5; case ((uint8_t)73ULL): goto ma9; case ((uint8_t)74ULL): goto ma9; case ((uint8_t)75ULL): goto ma9; case ((uint8_t)76ULL): goto ma9; case ((uint8_t)77ULL): goto ma9; case ((uint8_t)78ULL): goto ma9; case ((uint8_t)79ULL): goto ma6; case ((uint8_t)80ULL): goto ma7; case ((uint8_t)81ULL): goto ma8; default: goto ma9; }
ma3:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t13 != ((uint8_t)21ULL)) goto mn12;
    gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t14;
    gm_t9qkh0rsooc8d t15 = gm_l1;
    gm_b16_8 t16 = gm_f_flavor_text_3ffnbt7(t15);
    gm_tnpifv20irnph t17;
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t16);
    gm_tabb710d9mq2e t18;
    gm_f_format_qcp7nf9((void *)&t18, (void *)NULL, cx, NULL, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t19 = t18;
    if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srco4ljquuk5d87, 572, 16);
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
    gm_b16_8 t21 = t20;
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
    t1 = t22;
    goto me2;
mn12:;
ma4:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t24 != ((uint8_t)68ULL)) goto mn23;
    gm_t9qkh0rsooc8d t25 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 4));
    gm_l2 = t25;
    gm_t9qkh0rsooc8d t26 = gm_l2;
    gm_b16_8 t27 = gm_f_rmw_text_te4ol3u(t26);
    gm_tnpifv20irnph t28;
    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t27);
    gm_tabb710d9mq2e t29;
    gm_f_format_aq84o6u((void *)&t29, (void *)NULL, cx, NULL, t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t30 = t29;
    if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srco4ljquuk5d87, 573, 22);
    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
    gm_b16_8 t32 = t31;
    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
    t1 = t33;
    goto me2;
mn23:;
ma5:;
    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t35 != ((uint8_t)72ULL)) goto mn34;
    gm_t9qkh0rsooc8d t36 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 4));
    gm_l3 = t36;
    gm_t9qkh0rsooc8d t37 = gm_l3;
    gm_b16_8 t38 = gm_f_sys_text_9qjnpra(t37);
    gm_tnpifv20irnph t39;
    GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t38);
    gm_tabb710d9mq2e t40;
    gm_f_format_556kaa4((void *)&t40, (void *)NULL, cx, NULL, t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t41 = t40;
    if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 8, gm_srco4ljquuk5d87, 574, 16);
    gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&t41) + 8));
    gm_b16_8 t43 = t42;
    gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
    t1 = t44;
    goto me2;
mn34:;
ma6:;
    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t46 != ((uint8_t)79ULL)) goto mn45;
    uint32_t t47 = GM_LD(uint32_t, ((((uint8_t *)&gm_l0) + 4) + 0));
    gm_l4 = t47;
    uint32_t t48 = GM_LD(uint32_t, ((((uint8_t *)&gm_l0) + 4) + 4));
    gm_l5 = t48;
    uint32_t t49 = gm_l4;
    uint32_t t50 = gm_l5;
    gm_tcc1rq4n1avb2 t51;
    GM_ST(uint32_t, ((uint8_t *)&t51 + 0), t49);
    GM_ST(uint32_t, ((uint8_t *)&t51 + 4), t50);
    gm_tabb710d9mq2e t52;
    gm_f_format_c2ann39((void *)&t52, (void *)NULL, cx, NULL, t51);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t53 = t52;
    if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_srco4ljquuk5d87, 575, 30);
    gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
    gm_b16_8 t55 = t54;
    gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
    t1 = t56;
    goto me2;
mn45:;
ma7:;
    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t58 != ((uint8_t)80ULL)) goto mn57;
    uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    gm_l6 = t59;
    uint32_t t60 = gm_l6;
    gm_t88rcucg1b7uk t61;
    GM_ST(uint32_t, ((uint8_t *)&t61 + 0), t60);
    gm_tabb710d9mq2e t62;
    gm_f_format_9g7fct9((void *)&t62, (void *)NULL, cx, NULL, t61);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t63 = t62;
    if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srco4ljquuk5d87, 576, 26);
    gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
    gm_b16_8 t65 = t64;
    gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
    t1 = t66;
    goto me2;
mn57:;
ma8:;
    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t68 != ((uint8_t)81ULL)) goto mn67;
    uint32_t t69 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    gm_l7 = t69;
    uint32_t t70 = gm_l7;
    gm_t88rcucg1b7uk t71;
    GM_ST(uint32_t, ((uint8_t *)&t71 + 0), t70);
    gm_tabb710d9mq2e t72;
    gm_f_format_ree2v5u((void *)&t72, (void *)NULL, cx, NULL, t71);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t73 = t72;
    if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 8, gm_srco4ljquuk5d87, 577, 19);
    gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
    gm_b16_8 t75 = t74;
    gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
    t1 = t76;
    goto me2;
mn67:;
ma9:;
    gm_b16_8 t78;
    uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t163) { case ((uint8_t)0ULL): goto ma80; case ((uint8_t)1ULL): goto ma81; case ((uint8_t)2ULL): goto ma82; case ((uint8_t)3ULL): goto ma83; case ((uint8_t)4ULL): goto ma84; case ((uint8_t)5ULL): goto ma85; case ((uint8_t)6ULL): goto ma86; case ((uint8_t)7ULL): goto ma87; case ((uint8_t)8ULL): goto ma88; case ((uint8_t)9ULL): goto ma89; case ((uint8_t)10ULL): goto ma90; case ((uint8_t)11ULL): goto ma91; case ((uint8_t)12ULL): goto ma92; case ((uint8_t)13ULL): goto ma93; case ((uint8_t)14ULL): goto ma94; case ((uint8_t)15ULL): goto ma95; case ((uint8_t)16ULL): goto ma96; case ((uint8_t)17ULL): goto ma97; case ((uint8_t)18ULL): goto ma98; case ((uint8_t)19ULL): goto ma99; case ((uint8_t)20ULL): goto ma100; case ((uint8_t)21ULL): goto ma101; case ((uint8_t)22ULL): goto ma102; case ((uint8_t)23ULL): goto ma103; case ((uint8_t)24ULL): goto ma104; case ((uint8_t)25ULL): goto ma105; case ((uint8_t)26ULL): goto ma106; case ((uint8_t)27ULL): goto ma107; case ((uint8_t)28ULL): goto ma108; case ((uint8_t)29ULL): goto ma109; case ((uint8_t)30ULL): goto ma110; case ((uint8_t)31ULL): goto ma111; case ((uint8_t)32ULL): goto ma112; case ((uint8_t)33ULL): goto ma113; case ((uint8_t)34ULL): goto ma114; case ((uint8_t)35ULL): goto ma115; case ((uint8_t)36ULL): goto ma116; case ((uint8_t)37ULL): goto ma117; case ((uint8_t)38ULL): goto ma118; case ((uint8_t)39ULL): goto ma119; case ((uint8_t)40ULL): goto ma120; case ((uint8_t)41ULL): goto ma121; case ((uint8_t)42ULL): goto ma122; case ((uint8_t)43ULL): goto ma123; case ((uint8_t)44ULL): goto ma124; case ((uint8_t)45ULL): goto ma125; case ((uint8_t)46ULL): goto ma126; case ((uint8_t)47ULL): goto ma127; case ((uint8_t)48ULL): goto ma128; case ((uint8_t)49ULL): goto ma129; case ((uint8_t)50ULL): goto ma130; case ((uint8_t)51ULL): goto ma131; case ((uint8_t)52ULL): goto ma132; case ((uint8_t)53ULL): goto ma133; case ((uint8_t)54ULL): goto ma134; case ((uint8_t)55ULL): goto ma135; case ((uint8_t)56ULL): goto ma136; case ((uint8_t)57ULL): goto ma137; case ((uint8_t)58ULL): goto ma138; case ((uint8_t)59ULL): goto ma139; case ((uint8_t)60ULL): goto ma140; case ((uint8_t)61ULL): goto ma141; case ((uint8_t)62ULL): goto ma142; case ((uint8_t)63ULL): goto ma143; case ((uint8_t)64ULL): goto ma144; case ((uint8_t)65ULL): goto ma145; case ((uint8_t)66ULL): goto ma146; case ((uint8_t)67ULL): goto ma147; case ((uint8_t)68ULL): goto ma148; case ((uint8_t)69ULL): goto ma149; case ((uint8_t)70ULL): goto ma150; case ((uint8_t)71ULL): goto ma151; case ((uint8_t)72ULL): goto ma152; case ((uint8_t)73ULL): goto ma153; case ((uint8_t)74ULL): goto ma154; case ((uint8_t)75ULL): goto ma155; case ((uint8_t)76ULL): goto ma156; case ((uint8_t)77ULL): goto ma157; case ((uint8_t)78ULL): goto ma158; case ((uint8_t)79ULL): goto ma159; case ((uint8_t)80ULL): goto ma160; case ((uint8_t)81ULL): goto ma161; default: goto ma162; }
ma80:;
    uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t165 != ((uint8_t)0ULL)) goto mn164;
    gm_b16_8 t166;
    GM_ST(void *, (uint8_t *)&t166, (void *)(gm_sivsapeh482sm));
    GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(7));
    t78 = t166;
    goto me79;
mn164:;
ma81:;
    uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t168 != ((uint8_t)1ULL)) goto mn167;
    gm_b16_8 t169;
    GM_ST(void *, (uint8_t *)&t169, (void *)(gm_sk2b59ovo43qg));
    GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(5));
    t78 = t169;
    goto me79;
mn167:;
ma82:;
    uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t171 != ((uint8_t)2ULL)) goto mn170;
    gm_b16_8 t172;
    GM_ST(void *, (uint8_t *)&t172, (void *)(gm_s9dgf2prvg715));
    GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(5));
    t78 = t172;
    goto me79;
mn170:;
ma83:;
    uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t174 != ((uint8_t)3ULL)) goto mn173;
    gm_b16_8 t175;
    GM_ST(void *, (uint8_t *)&t175, (void *)(gm_scb853lmgr58k));
    GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(5));
    t78 = t175;
    goto me79;
mn173:;
ma84:;
    uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t177 != ((uint8_t)4ULL)) goto mn176;
    gm_b16_8 t178;
    GM_ST(void *, (uint8_t *)&t178, (void *)(gm_seeo26ergtcha));
    GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(4));
    t78 = t178;
    goto me79;
mn176:;
ma85:;
    uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t180 != ((uint8_t)5ULL)) goto mn179;
    gm_b16_8 t181;
    GM_ST(void *, (uint8_t *)&t181, (void *)(gm_sdft8ov6bb7s6));
    GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(7));
    t78 = t181;
    goto me79;
mn179:;
ma86:;
    uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t183 != ((uint8_t)6ULL)) goto mn182;
    gm_b16_8 t184;
    GM_ST(void *, (uint8_t *)&t184, (void *)(gm_sv3olmne7h13s));
    GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(3));
    t78 = t184;
    goto me79;
mn182:;
ma87:;
    uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t186 != ((uint8_t)7ULL)) goto mn185;
    gm_b16_8 t187;
    GM_ST(void *, (uint8_t *)&t187, (void *)(gm_skapbb3maknat));
    GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(4));
    t78 = t187;
    goto me79;
mn185:;
ma88:;
    uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t189 != ((uint8_t)8ULL)) goto mn188;
    gm_b16_8 t190;
    GM_ST(void *, (uint8_t *)&t190, (void *)(gm_scd599tsljaee));
    GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(4));
    t78 = t190;
    goto me79;
mn188:;
ma89:;
    uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t192 != ((uint8_t)9ULL)) goto mn191;
    gm_b16_8 t193;
    GM_ST(void *, (uint8_t *)&t193, (void *)(gm_s2kmbpuutp9qq));
    GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(4));
    t78 = t193;
    goto me79;
mn191:;
ma90:;
    uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t195 != ((uint8_t)10ULL)) goto mn194;
    gm_b16_8 t196;
    GM_ST(void *, (uint8_t *)&t196, (void *)(gm_s7u6ij52clodh));
    GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(6));
    t78 = t196;
    goto me79;
mn194:;
ma91:;
    uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t198 != ((uint8_t)11ULL)) goto mn197;
    gm_b16_8 t199;
    GM_ST(void *, (uint8_t *)&t199, (void *)(gm_sun4qq3op5opf));
    GM_ST(uintptr_t, (uint8_t *)&t199 + 8, (uintptr_t)(6));
    t78 = t199;
    goto me79;
mn197:;
ma92:;
    uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t201 != ((uint8_t)12ULL)) goto mn200;
    gm_b16_8 t202;
    GM_ST(void *, (uint8_t *)&t202, (void *)(gm_s2h8cr50573go));
    GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(7));
    t78 = t202;
    goto me79;
mn200:;
ma93:;
    uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t204 != ((uint8_t)13ULL)) goto mn203;
    gm_b16_8 t205;
    GM_ST(void *, (uint8_t *)&t205, (void *)(gm_s00pihipe0jga));
    GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(5));
    t78 = t205;
    goto me79;
mn203:;
ma94:;
    uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t207 != ((uint8_t)14ULL)) goto mn206;
    gm_b16_8 t208;
    GM_ST(void *, (uint8_t *)&t208, (void *)(gm_s3opc5ivp6e66));
    GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(3));
    t78 = t208;
    goto me79;
mn206:;
ma95:;
    uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t210 != ((uint8_t)15ULL)) goto mn209;
    gm_b16_8 t211;
    GM_ST(void *, (uint8_t *)&t211, (void *)(gm_sj1m58fkg956m));
    GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(7));
    t78 = t211;
    goto me79;
mn209:;
ma96:;
    uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t213 != ((uint8_t)16ULL)) goto mn212;
    gm_b16_8 t214;
    GM_ST(void *, (uint8_t *)&t214, (void *)(gm_s187rb18i1kgk));
    GM_ST(uintptr_t, (uint8_t *)&t214 + 8, (uintptr_t)(3));
    t78 = t214;
    goto me79;
mn212:;
ma97:;
    uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t216 != ((uint8_t)17ULL)) goto mn215;
    gm_b16_8 t217;
    GM_ST(void *, (uint8_t *)&t217, (void *)(gm_sgo00f0mafbnd));
    GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(3));
    t78 = t217;
    goto me79;
mn215:;
ma98:;
    uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t219 != ((uint8_t)18ULL)) goto mn218;
    gm_b16_8 t220;
    GM_ST(void *, (uint8_t *)&t220, (void *)(gm_s5udr3eget84q));
    GM_ST(uintptr_t, (uint8_t *)&t220 + 8, (uintptr_t)(5));
    t78 = t220;
    goto me79;
mn218:;
ma99:;
    uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t222 != ((uint8_t)19ULL)) goto mn221;
    gm_b16_8 t223;
    GM_ST(void *, (uint8_t *)&t223, (void *)(gm_sfd0mn3sol25u));
    GM_ST(uintptr_t, (uint8_t *)&t223 + 8, (uintptr_t)(3));
    t78 = t223;
    goto me79;
mn221:;
ma100:;
    uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t225 != ((uint8_t)20ULL)) goto mn224;
    gm_b16_8 t226;
    GM_ST(void *, (uint8_t *)&t226, (void *)(gm_s85hqf14lbkns));
    GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(7));
    t78 = t226;
    goto me79;
mn224:;
ma101:;
    uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t228 != ((uint8_t)21ULL)) goto mn227;
    gm_b16_8 t229;
    GM_ST(void *, (uint8_t *)&t229, (void *)(gm_s6qbnfe2f321h));
    GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(3));
    t78 = t229;
    goto me79;
mn227:;
ma102:;
    uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t231 != ((uint8_t)22ULL)) goto mn230;
    gm_b16_8 t232;
    GM_ST(void *, (uint8_t *)&t232, (void *)(gm_sds03accef1j8));
    GM_ST(uintptr_t, (uint8_t *)&t232 + 8, (uintptr_t)(5));
    t78 = t232;
    goto me79;
mn230:;
ma103:;
    uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t234 != ((uint8_t)23ULL)) goto mn233;
    gm_b16_8 t235;
    GM_ST(void *, (uint8_t *)&t235, (void *)(gm_s24chpb914bep));
    GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(5));
    t78 = t235;
    goto me79;
mn233:;
ma104:;
    uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t237 != ((uint8_t)24ULL)) goto mn236;
    gm_b16_8 t238;
    GM_ST(void *, (uint8_t *)&t238, (void *)(gm_su3ahsl9p584f));
    GM_ST(uintptr_t, (uint8_t *)&t238 + 8, (uintptr_t)(8));
    t78 = t238;
    goto me79;
mn236:;
ma105:;
    uint8_t t240 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t240 != ((uint8_t)25ULL)) goto mn239;
    gm_b16_8 t241;
    GM_ST(void *, (uint8_t *)&t241, (void *)(gm_s28ijm46j20ds));
    GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(7));
    t78 = t241;
    goto me79;
mn239:;
ma106:;
    uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t243 != ((uint8_t)26ULL)) goto mn242;
    gm_b16_8 t244;
    GM_ST(void *, (uint8_t *)&t244, (void *)(gm_sv8lqdk5nsv8i));
    GM_ST(uintptr_t, (uint8_t *)&t244 + 8, (uintptr_t)(4));
    t78 = t244;
    goto me79;
mn242:;
ma107:;
    uint8_t t246 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t246 != ((uint8_t)27ULL)) goto mn245;
    gm_b16_8 t247;
    GM_ST(void *, (uint8_t *)&t247, (void *)(gm_s1q9ms0kviddj));
    GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(3));
    t78 = t247;
    goto me79;
mn245:;
ma108:;
    uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t249 != ((uint8_t)28ULL)) goto mn248;
    gm_b16_8 t250;
    GM_ST(void *, (uint8_t *)&t250, (void *)(gm_s2nl2e5anglov));
    GM_ST(uintptr_t, (uint8_t *)&t250 + 8, (uintptr_t)(3));
    t78 = t250;
    goto me79;
mn248:;
ma109:;
    uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t252 != ((uint8_t)29ULL)) goto mn251;
    gm_b16_8 t253;
    GM_ST(void *, (uint8_t *)&t253, (void *)(gm_s0kvnv9teor3q));
    GM_ST(uintptr_t, (uint8_t *)&t253 + 8, (uintptr_t)(3));
    t78 = t253;
    goto me79;
mn251:;
ma110:;
    uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t255 != ((uint8_t)30ULL)) goto mn254;
    gm_b16_8 t256;
    GM_ST(void *, (uint8_t *)&t256, (void *)(gm_solj59tk4ri3k));
    GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(8));
    t78 = t256;
    goto me79;
mn254:;
ma111:;
    uint8_t t258 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t258 != ((uint8_t)31ULL)) goto mn257;
    gm_b16_8 t259;
    GM_ST(void *, (uint8_t *)&t259, (void *)(gm_sqhhm0h7b3asd));
    GM_ST(uintptr_t, (uint8_t *)&t259 + 8, (uintptr_t)(5));
    t78 = t259;
    goto me79;
mn257:;
ma112:;
    uint8_t t261 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t261 != ((uint8_t)32ULL)) goto mn260;
    gm_b16_8 t262;
    GM_ST(void *, (uint8_t *)&t262, (void *)(gm_s3ng5nguvd452));
    GM_ST(uintptr_t, (uint8_t *)&t262 + 8, (uintptr_t)(4));
    t78 = t262;
    goto me79;
mn260:;
ma113:;
    uint8_t t264 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t264 != ((uint8_t)33ULL)) goto mn263;
    gm_b16_8 t265;
    GM_ST(void *, (uint8_t *)&t265, (void *)(gm_sjbl17pjsjm48));
    GM_ST(uintptr_t, (uint8_t *)&t265 + 8, (uintptr_t)(4));
    t78 = t265;
    goto me79;
mn263:;
ma114:;
    uint8_t t267 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t267 != ((uint8_t)34ULL)) goto mn266;
    gm_b16_8 t268;
    GM_ST(void *, (uint8_t *)&t268, (void *)(gm_sl37uvr16qsu8));
    GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(10));
    t78 = t268;
    goto me79;
mn266:;
ma115:;
    uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t270 != ((uint8_t)35ULL)) goto mn269;
    gm_b16_8 t271;
    GM_ST(void *, (uint8_t *)&t271, (void *)(gm_s1q2nkavemrqf));
    GM_ST(uintptr_t, (uint8_t *)&t271 + 8, (uintptr_t)(8));
    t78 = t271;
    goto me79;
mn269:;
ma116:;
    uint8_t t273 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t273 != ((uint8_t)36ULL)) goto mn272;
    gm_b16_8 t274;
    GM_ST(void *, (uint8_t *)&t274, (void *)(gm_s2a9t8smkevg0));
    GM_ST(uintptr_t, (uint8_t *)&t274 + 8, (uintptr_t)(3));
    t78 = t274;
    goto me79;
mn272:;
ma117:;
    uint8_t t276 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t276 != ((uint8_t)37ULL)) goto mn275;
    gm_b16_8 t277;
    GM_ST(void *, (uint8_t *)&t277, (void *)(gm_sa1pcp92k3aj3));
    GM_ST(uintptr_t, (uint8_t *)&t277 + 8, (uintptr_t)(6));
    t78 = t277;
    goto me79;
mn275:;
ma118:;
    uint8_t t279 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t279 != ((uint8_t)38ULL)) goto mn278;
    gm_b16_8 t280;
    GM_ST(void *, (uint8_t *)&t280, (void *)(gm_shkq4botdoed7));
    GM_ST(uintptr_t, (uint8_t *)&t280 + 8, (uintptr_t)(5));
    t78 = t280;
    goto me79;
mn278:;
ma119:;
    uint8_t t282 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t282 != ((uint8_t)39ULL)) goto mn281;
    gm_b16_8 t283;
    GM_ST(void *, (uint8_t *)&t283, (void *)(gm_sse9dgkusbtnf));
    GM_ST(uintptr_t, (uint8_t *)&t283 + 8, (uintptr_t)(4));
    t78 = t283;
    goto me79;
mn281:;
ma120:;
    uint8_t t285 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t285 != ((uint8_t)40ULL)) goto mn284;
    gm_b16_8 t286;
    GM_ST(void *, (uint8_t *)&t286, (void *)(gm_si4k9tiofr8t2));
    GM_ST(uintptr_t, (uint8_t *)&t286 + 8, (uintptr_t)(6));
    t78 = t286;
    goto me79;
mn284:;
ma121:;
    uint8_t t288 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t288 != ((uint8_t)41ULL)) goto mn287;
    gm_b16_8 t289;
    GM_ST(void *, (uint8_t *)&t289, (void *)(gm_s6aamtn65omr1));
    GM_ST(uintptr_t, (uint8_t *)&t289 + 8, (uintptr_t)(6));
    t78 = t289;
    goto me79;
mn287:;
ma122:;
    uint8_t t291 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t291 != ((uint8_t)42ULL)) goto mn290;
    gm_b16_8 t292;
    GM_ST(void *, (uint8_t *)&t292, (void *)(gm_s8s6shfnaqahd));
    GM_ST(uintptr_t, (uint8_t *)&t292 + 8, (uintptr_t)(6));
    t78 = t292;
    goto me79;
mn290:;
ma123:;
    uint8_t t294 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t294 != ((uint8_t)43ULL)) goto mn293;
    gm_b16_8 t295;
    GM_ST(void *, (uint8_t *)&t295, (void *)(gm_sbnms71r2jqr5));
    GM_ST(uintptr_t, (uint8_t *)&t295 + 8, (uintptr_t)(6));
    t78 = t295;
    goto me79;
mn293:;
ma124:;
    uint8_t t297 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t297 != ((uint8_t)44ULL)) goto mn296;
    gm_b16_8 t298;
    GM_ST(void *, (uint8_t *)&t298, (void *)(gm_ss27a20sarlvg));
    GM_ST(uintptr_t, (uint8_t *)&t298 + 8, (uintptr_t)(6));
    t78 = t298;
    goto me79;
mn296:;
ma125:;
    uint8_t t300 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t300 != ((uint8_t)45ULL)) goto mn299;
    gm_b16_8 t301;
    GM_ST(void *, (uint8_t *)&t301, (void *)(gm_sbek54n4a6h2o));
    GM_ST(uintptr_t, (uint8_t *)&t301 + 8, (uintptr_t)(6));
    t78 = t301;
    goto me79;
mn299:;
ma126:;
    uint8_t t303 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t303 != ((uint8_t)46ULL)) goto mn302;
    gm_b16_8 t304;
    GM_ST(void *, (uint8_t *)&t304, (void *)(gm_std59a9b6lpnn));
    GM_ST(uintptr_t, (uint8_t *)&t304 + 8, (uintptr_t)(9));
    t78 = t304;
    goto me79;
mn302:;
ma127:;
    uint8_t t306 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t306 != ((uint8_t)47ULL)) goto mn305;
    gm_b16_8 t307;
    GM_ST(void *, (uint8_t *)&t307, (void *)(gm_ssjmv3618n779));
    GM_ST(uintptr_t, (uint8_t *)&t307 + 8, (uintptr_t)(9));
    t78 = t307;
    goto me79;
mn305:;
ma128:;
    uint8_t t309 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t309 != ((uint8_t)48ULL)) goto mn308;
    gm_b16_8 t310;
    GM_ST(void *, (uint8_t *)&t310, (void *)(gm_smuec3qfsdh82));
    GM_ST(uintptr_t, (uint8_t *)&t310 + 8, (uintptr_t)(9));
    t78 = t310;
    goto me79;
mn308:;
ma129:;
    uint8_t t312 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t312 != ((uint8_t)49ULL)) goto mn311;
    gm_b16_8 t313;
    GM_ST(void *, (uint8_t *)&t313, (void *)(gm_s57rgvjb5mj5a));
    GM_ST(uintptr_t, (uint8_t *)&t313 + 8, (uintptr_t)(7));
    t78 = t313;
    goto me79;
mn311:;
ma130:;
    uint8_t t315 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t315 != ((uint8_t)50ULL)) goto mn314;
    gm_b16_8 t316;
    GM_ST(void *, (uint8_t *)&t316, (void *)(gm_s76ekntop342o));
    GM_ST(uintptr_t, (uint8_t *)&t316 + 8, (uintptr_t)(7));
    t78 = t316;
    goto me79;
mn314:;
ma131:;
    uint8_t t318 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t318 != ((uint8_t)51ULL)) goto mn317;
    gm_b16_8 t319;
    GM_ST(void *, (uint8_t *)&t319, (void *)(gm_sm3dios51ece1));
    GM_ST(uintptr_t, (uint8_t *)&t319 + 8, (uintptr_t)(9));
    t78 = t319;
    goto me79;
mn317:;
ma132:;
    uint8_t t321 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t321 != ((uint8_t)52ULL)) goto mn320;
    gm_b16_8 t322;
    GM_ST(void *, (uint8_t *)&t322, (void *)(gm_s9h93c7asacna));
    GM_ST(uintptr_t, (uint8_t *)&t322 + 8, (uintptr_t)(6));
    t78 = t322;
    goto me79;
mn320:;
ma133:;
    uint8_t t324 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t324 != ((uint8_t)53ULL)) goto mn323;
    gm_b16_8 t325;
    GM_ST(void *, (uint8_t *)&t325, (void *)(gm_s8pk5tcnrk6p2));
    GM_ST(uintptr_t, (uint8_t *)&t325 + 8, (uintptr_t)(7));
    t78 = t325;
    goto me79;
mn323:;
ma134:;
    uint8_t t327 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t327 != ((uint8_t)54ULL)) goto mn326;
    gm_b16_8 t328;
    GM_ST(void *, (uint8_t *)&t328, (void *)(gm_svqq3k9j6h1lu));
    GM_ST(uintptr_t, (uint8_t *)&t328 + 8, (uintptr_t)(7));
    t78 = t328;
    goto me79;
mn326:;
ma135:;
    uint8_t t330 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t330 != ((uint8_t)55ULL)) goto mn329;
    gm_b16_8 t331;
    GM_ST(void *, (uint8_t *)&t331, (void *)(gm_sdh1dnevn1j10));
    GM_ST(uintptr_t, (uint8_t *)&t331 + 8, (uintptr_t)(7));
    t78 = t331;
    goto me79;
mn329:;
ma136:;
    uint8_t t333 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t333 != ((uint8_t)56ULL)) goto mn332;
    gm_b16_8 t334;
    GM_ST(void *, (uint8_t *)&t334, (void *)(gm_sij2r6jhpquej));
    GM_ST(uintptr_t, (uint8_t *)&t334 + 8, (uintptr_t)(11));
    t78 = t334;
    goto me79;
mn332:;
ma137:;
    uint8_t t336 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t336 != ((uint8_t)57ULL)) goto mn335;
    gm_b16_8 t337;
    GM_ST(void *, (uint8_t *)&t337, (void *)(gm_stft4dc50tdce));
    GM_ST(uintptr_t, (uint8_t *)&t337 + 8, (uintptr_t)(7));
    t78 = t337;
    goto me79;
mn335:;
ma138:;
    uint8_t t339 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t339 != ((uint8_t)58ULL)) goto mn338;
    gm_b16_8 t340;
    GM_ST(void *, (uint8_t *)&t340, (void *)(gm_sa0alba1kin3k));
    GM_ST(uintptr_t, (uint8_t *)&t340 + 8, (uintptr_t)(7));
    t78 = t340;
    goto me79;
mn338:;
ma139:;
    uint8_t t342 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t342 != ((uint8_t)59ULL)) goto mn341;
    gm_b16_8 t343;
    GM_ST(void *, (uint8_t *)&t343, (void *)(gm_s9o8gb20t6jtp));
    GM_ST(uintptr_t, (uint8_t *)&t343 + 8, (uintptr_t)(8));
    t78 = t343;
    goto me79;
mn341:;
ma140:;
    uint8_t t345 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t345 != ((uint8_t)60ULL)) goto mn344;
    gm_b16_8 t346;
    GM_ST(void *, (uint8_t *)&t346, (void *)(gm_sh11qqm8nqu68));
    GM_ST(uintptr_t, (uint8_t *)&t346 + 8, (uintptr_t)(7));
    t78 = t346;
    goto me79;
mn344:;
ma141:;
    uint8_t t348 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t348 != ((uint8_t)61ULL)) goto mn347;
    gm_b16_8 t349;
    GM_ST(void *, (uint8_t *)&t349, (void *)(gm_s2fu5nkek0lrq));
    GM_ST(uintptr_t, (uint8_t *)&t349 + 8, (uintptr_t)(6));
    t78 = t349;
    goto me79;
mn347:;
ma142:;
    uint8_t t351 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t351 != ((uint8_t)62ULL)) goto mn350;
    gm_b16_8 t352;
    GM_ST(void *, (uint8_t *)&t352, (void *)(gm_s7i80akiq0d95));
    GM_ST(uintptr_t, (uint8_t *)&t352 + 8, (uintptr_t)(4));
    t78 = t352;
    goto me79;
mn350:;
ma143:;
    uint8_t t354 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t354 != ((uint8_t)63ULL)) goto mn353;
    gm_b16_8 t355;
    GM_ST(void *, (uint8_t *)&t355, (void *)(gm_stn52kuiq0jk2));
    GM_ST(uintptr_t, (uint8_t *)&t355 + 8, (uintptr_t)(11));
    t78 = t355;
    goto me79;
mn353:;
ma144:;
    uint8_t t357 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t357 != ((uint8_t)64ULL)) goto mn356;
    gm_b16_8 t358;
    GM_ST(void *, (uint8_t *)&t358, (void *)(gm_srjd0sq8k3d7s));
    GM_ST(uintptr_t, (uint8_t *)&t358 + 8, (uintptr_t)(10));
    t78 = t358;
    goto me79;
mn356:;
ma145:;
    uint8_t t360 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t360 != ((uint8_t)65ULL)) goto mn359;
    gm_b16_8 t361;
    GM_ST(void *, (uint8_t *)&t361, (void *)(gm_sdq6l7oddjqfe));
    GM_ST(uintptr_t, (uint8_t *)&t361 + 8, (uintptr_t)(11));
    t78 = t361;
    goto me79;
mn359:;
ma146:;
    uint8_t t363 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t363 != ((uint8_t)66ULL)) goto mn362;
    gm_b16_8 t364;
    GM_ST(void *, (uint8_t *)&t364, (void *)(gm_s6ipt60dh9o1j));
    GM_ST(uintptr_t, (uint8_t *)&t364 + 8, (uintptr_t)(10));
    t78 = t364;
    goto me79;
mn362:;
ma147:;
    uint8_t t366 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t366 != ((uint8_t)67ULL)) goto mn365;
    gm_b16_8 t367;
    GM_ST(void *, (uint8_t *)&t367, (void *)(gm_sa53epbg9iarq));
    GM_ST(uintptr_t, (uint8_t *)&t367 + 8, (uintptr_t)(9));
    t78 = t367;
    goto me79;
mn365:;
ma148:;
    uint8_t t369 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t369 != ((uint8_t)68ULL)) goto mn368;
    gm_b16_8 t370;
    GM_ST(void *, (uint8_t *)&t370, (void *)(gm_s2fdsodctojm1));
    GM_ST(uintptr_t, (uint8_t *)&t370 + 8, (uintptr_t)(9));
    t78 = t370;
    goto me79;
mn368:;
ma149:;
    uint8_t t372 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t372 != ((uint8_t)69ULL)) goto mn371;
    gm_b16_8 t373;
    GM_ST(void *, (uint8_t *)&t373, (void *)(gm_skbgm82aqj3q0));
    GM_ST(uintptr_t, (uint8_t *)&t373 + 8, (uintptr_t)(5));
    t78 = t373;
    goto me79;
mn371:;
ma150:;
    uint8_t t375 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t375 != ((uint8_t)70ULL)) goto mn374;
    gm_b16_8 t376;
    GM_ST(void *, (uint8_t *)&t376, (void *)(gm_sk88tp2337lf6));
    GM_ST(uintptr_t, (uint8_t *)&t376 + 8, (uintptr_t)(12));
    t78 = t376;
    goto me79;
mn374:;
ma151:;
    uint8_t t378 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t378 != ((uint8_t)71ULL)) goto mn377;
    gm_b16_8 t379;
    GM_ST(void *, (uint8_t *)&t379, (void *)(gm_st0l9v6pp5i7b));
    GM_ST(uintptr_t, (uint8_t *)&t379 + 8, (uintptr_t)(13));
    t78 = t379;
    goto me79;
mn377:;
ma152:;
    uint8_t t381 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t381 != ((uint8_t)72ULL)) goto mn380;
    gm_b16_8 t382;
    GM_ST(void *, (uint8_t *)&t382, (void *)(gm_sa902c1tbtg0i));
    GM_ST(uintptr_t, (uint8_t *)&t382 + 8, (uintptr_t)(3));
    t78 = t382;
    goto me79;
mn380:;
ma153:;
    uint8_t t384 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t384 != ((uint8_t)73ULL)) goto mn383;
    gm_b16_8 t385;
    GM_ST(void *, (uint8_t *)&t385, (void *)(gm_ss869lnvmj3m8));
    GM_ST(uintptr_t, (uint8_t *)&t385 + 8, (uintptr_t)(5));
    t78 = t385;
    goto me79;
mn383:;
ma154:;
    uint8_t t387 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t387 != ((uint8_t)74ULL)) goto mn386;
    gm_b16_8 t388;
    GM_ST(void *, (uint8_t *)&t388, (void *)(gm_s1j5448m6ruti));
    GM_ST(uintptr_t, (uint8_t *)&t388 + 8, (uintptr_t)(9));
    t78 = t388;
    goto me79;
mn386:;
ma155:;
    uint8_t t390 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t390 != ((uint8_t)75ULL)) goto mn389;
    gm_b16_8 t391;
    GM_ST(void *, (uint8_t *)&t391, (void *)(gm_src5778gpfoh9));
    GM_ST(uintptr_t, (uint8_t *)&t391 + 8, (uintptr_t)(8));
    t78 = t391;
    goto me79;
mn389:;
ma156:;
    uint8_t t393 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t393 != ((uint8_t)76ULL)) goto mn392;
    gm_b16_8 t394;
    GM_ST(void *, (uint8_t *)&t394, (void *)(gm_smpl91sh0k8v4));
    GM_ST(uintptr_t, (uint8_t *)&t394 + 8, (uintptr_t)(8));
    t78 = t394;
    goto me79;
mn392:;
ma157:;
    uint8_t t396 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t396 != ((uint8_t)77ULL)) goto mn395;
    gm_b16_8 t397;
    GM_ST(void *, (uint8_t *)&t397, (void *)(gm_s7rltjaeh634d));
    GM_ST(uintptr_t, (uint8_t *)&t397 + 8, (uintptr_t)(10));
    t78 = t397;
    goto me79;
mn395:;
ma158:;
    uint8_t t399 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t399 != ((uint8_t)78ULL)) goto mn398;
    gm_b16_8 t400;
    GM_ST(void *, (uint8_t *)&t400, (void *)(gm_s5c5548ll8taj));
    GM_ST(uintptr_t, (uint8_t *)&t400 + 8, (uintptr_t)(9));
    t78 = t400;
    goto me79;
mn398:;
ma159:;
    uint8_t t402 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t402 != ((uint8_t)79ULL)) goto mn401;
    gm_b16_8 t403;
    GM_ST(void *, (uint8_t *)&t403, (void *)(gm_seodvmll3gsjn));
    GM_ST(uintptr_t, (uint8_t *)&t403 + 8, (uintptr_t)(3));
    t78 = t403;
    goto me79;
mn401:;
ma160:;
    uint8_t t405 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t405 != ((uint8_t)80ULL)) goto mn404;
    gm_b16_8 t406;
    GM_ST(void *, (uint8_t *)&t406, (void *)(gm_st8ki5f85lc2j));
    GM_ST(uintptr_t, (uint8_t *)&t406 + 8, (uintptr_t)(12));
    t78 = t406;
    goto me79;
mn404:;
ma161:;
    uint8_t t408 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t408 != ((uint8_t)81ULL)) goto mn407;
    gm_b16_8 t409;
    GM_ST(void *, (uint8_t *)&t409, (void *)(gm_sepu95c7o773o));
    GM_ST(uintptr_t, (uint8_t *)&t409 + 8, (uintptr_t)(5));
    t78 = t409;
    goto me79;
mn407:;
ma162:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 578, 10);
me79:;
    t1 = t78;
    goto me2;
mn77:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 571, 62);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_coercion_text_g32oppe(void *env, gm_node *cx, gm_node *inst, gm_tmn7n0n9raou5 a0) {
    struct gm_fr_coercion_text_g32oppe F;
    gm_tmn7n0n9raou5 gm_l0; /* c */
    uint32_t gm_l1; /* v */
    GM_STACK_CHECK(&F, 407u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)4ULL)) goto mn3;
    uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t5;
    uint32_t t6 = gm_l1;
    gm_t88rcucg1b7uk t7;
    GM_ST(uint32_t, ((uint8_t *)&t7 + 0), t6);
    gm_tabb710d9mq2e t8;
    gm_f_format_41c1370((void *)&t8, (void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t9 = t8;
    if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_srco4ljquuk5d87, 424, 20);
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
    gm_b16_8 t11 = t10;
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
    t1 = t12;
    goto me2;
mn3:;
    gm_b16_8 t14;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t30) { case ((uint8_t)0ULL): goto ma16; case ((uint8_t)1ULL): goto ma17; case ((uint8_t)2ULL): goto ma18; case ((uint8_t)3ULL): goto ma19; case ((uint8_t)4ULL): goto ma20; case ((uint8_t)5ULL): goto ma21; case ((uint8_t)6ULL): goto ma22; case ((uint8_t)7ULL): goto ma23; case ((uint8_t)8ULL): goto ma24; case ((uint8_t)9ULL): goto ma25; case ((uint8_t)10ULL): goto ma26; case ((uint8_t)11ULL): goto ma27; case ((uint8_t)12ULL): goto ma28; default: goto ma29; }
ma16:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t32 != ((uint8_t)0ULL)) goto mn31;
    gm_b16_8 t33;
    GM_ST(void *, (uint8_t *)&t33, (void *)(gm_stivl0smu8eq0));
    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(8));
    t14 = t33;
    goto me15;
mn31:;
ma17:;
    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t35 != ((uint8_t)1ULL)) goto mn34;
    gm_b16_8 t36;
    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s2uqljf8sumcn));
    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(10));
    t14 = t36;
    goto me15;
mn34:;
ma18:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t38 != ((uint8_t)2ULL)) goto mn37;
    gm_b16_8 t39;
    GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sbckpj1jsdt28));
    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(12));
    t14 = t39;
    goto me15;
mn37:;
ma19:;
    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t41 != ((uint8_t)3ULL)) goto mn40;
    gm_b16_8 t42;
    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s6p9ub6gu603m));
    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(6));
    t14 = t42;
    goto me15;
mn40:;
ma20:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t44 != ((uint8_t)4ULL)) goto mn43;
    gm_b16_8 t45;
    GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s4g3rq58567m6));
    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(7));
    t14 = t45;
    goto me15;
mn43:;
ma21:;
    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t47 != ((uint8_t)5ULL)) goto mn46;
    gm_b16_8 t48;
    GM_ST(void *, (uint8_t *)&t48, (void *)(gm_stou1l3qp844f));
    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(9));
    t14 = t48;
    goto me15;
mn46:;
ma22:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t50 != ((uint8_t)6ULL)) goto mn49;
    gm_b16_8 t51;
    GM_ST(void *, (uint8_t *)&t51, (void *)(gm_st1pc9bsku00q));
    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(8));
    t14 = t51;
    goto me15;
mn49:;
ma23:;
    uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t53 != ((uint8_t)7ULL)) goto mn52;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(gm_s0eqrtq5mqb3a));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(15));
    t14 = t54;
    goto me15;
mn52:;
ma24:;
    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t56 != ((uint8_t)8ULL)) goto mn55;
    gm_b16_8 t57;
    GM_ST(void *, (uint8_t *)&t57, (void *)(gm_se0ivu8cf46o8));
    GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(11));
    t14 = t57;
    goto me15;
mn55:;
ma25:;
    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t59 != ((uint8_t)9ULL)) goto mn58;
    gm_b16_8 t60;
    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s96j6o9smoqdu));
    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(7));
    t14 = t60;
    goto me15;
mn58:;
ma26:;
    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t62 != ((uint8_t)10ULL)) goto mn61;
    gm_b16_8 t63;
    GM_ST(void *, (uint8_t *)&t63, (void *)(gm_srmrjv19jgvks));
    GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(10));
    t14 = t63;
    goto me15;
mn61:;
ma27:;
    uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t65 != ((uint8_t)11ULL)) goto mn64;
    gm_b16_8 t66;
    GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s55ccd7do7rbs));
    GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(10));
    t14 = t66;
    goto me15;
mn64:;
ma28:;
    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t68 != ((uint8_t)12ULL)) goto mn67;
    gm_b16_8 t69;
    GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sim8pu6stc37a));
    GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(10));
    t14 = t69;
    goto me15;
mn67:;
ma29:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 425, 10);
me15:;
    t1 = t14;
    goto me2;
mn13:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 423, 63);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_escaped_k5fm4jl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_escaped_k5fm4jl F;
    gm_b16_8 gm_l0; /* prefix */
    gm_b16_8 gm_l1; /* bytes */
    GM_STACK_CHECK(&F, 165u);
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
        void * t2 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srco4ljquuk5d87, 408, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        gm_b16_8 t7 = gm_l1;
        void * t8 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t9 = gm_f_escape_bytes_25mo226((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t11 = t10;
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        uint8_t t13 = 0;
        F.ret = t12;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_flavor_text_3ffnbt7(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_flavor_text_3ffnbt7 F;
    gm_t9qkh0rsooc8d gm_l0; /* f */
    GM_STACK_CHECK(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; default: goto ma7; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)0ULL)) goto mn9;
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sdft8ov6bb7s6));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(7));
    t1 = t11;
    goto me2;
mn9:;
ma4:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t13 != ((uint8_t)1ULL)) goto mn12;
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_ssgeom7733jik));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(4));
    t1 = t14;
    goto me2;
mn12:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)2ULL)) goto mn15;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sv3olmne7h13s));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(3));
    t1 = t17;
    goto me2;
mn15:;
ma6:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)3ULL)) goto mn18;
    gm_b16_8 t20;
    GM_ST(void *, (uint8_t *)&t20, (void *)(gm_setcl7798ov89));
    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
    t1 = t20;
    goto me2;
mn18:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 421, 47);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_label_9hbn37h(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_fn_label_9hbn37h F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* sig */
    gm_tiilmfd16v3fc gm_l3; /* inst */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* a */
    GM_STACK_CHECK(&F, 861u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 344) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srco4ljquuk5d87, 44, 13);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 344) + 0)) + t4 * 136);
        gm_l2 = t7;
    }
    {
        void * t9 = gm_l2;
        if (!(((((uint8_t *)t9) + 96))[24] != 0)) goto ie8;
        gm_tiilmfd16v3fc t10 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)t9) + 96));
        gm_l3 = t10;
        {
            gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
                gm_l5 = t13;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t15 = gm_l6;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t17 = (t15 < t16);
                    if (!t17) goto lb14;
                    {
                        uint64_t t20 = gm_l6;
                        uint64_t t21 = (uintptr_t)t20;
                        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t21 * 4));
                        gm_l7 = t25;
                        {
                            void * t27 = (void *)((uint8_t *)&F.l4);
                            void * t28 = gm_l0;
                            gm_t88rcucg1b7uk t29 = gm_l7;
                            gm_b16_8 t30 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t28, t29);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                            gm_tftb1odc739t5 t31 = gm_f_push_3ef746b(t27, t30);
                            gm_tftb1odc739t5 t32 = t31;
                            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srco4ljquuk5d87, 47, 30);
                            gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
                        }
                        goto me23;
mn24:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 47, 9);
me23:;
                    }
lc14:;
                    {
                        uint64_t t35 = gm_l6;
                        uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                        uint64_t t38 = r36;
                        gm_l6 = t38;
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
            gm_t6j6smj7qoctu t39;
            void * t40 = gm_l0;
            uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t42 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t39 + 0, t42);
            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_b16_8 t44 = t43;
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t47;
            gm_f_join_sfknljb((void *)&t47, (void *)NULL, cx, NULL, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t48 = t47;
            if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 8, gm_srco4ljquuk5d87, 48, 65);
            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&t48) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t39 + 16, t49);
            gm_tabb710d9mq2e t50;
            gm_f_format_licpkoa((void *)&t50, (void *)NULL, cx, NULL, &t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t51 = t50;
            if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_srco4ljquuk5d87, 48, 16);
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
            gm_b16_8 t53 = t52;
            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
            uint8_t t55 = 0;
            F.ret = t54;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t57 = gm_l2;
        gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t57) + 16));
        gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        gm_tl874t2atnlh3 t60 = t58;
        gm_tl874t2atnlh3 t61 = t59;
        uint8_t t62 = (gm_eqff85ac10o3(((uint8_t *)&t60), ((uint8_t *)&t61)));
        if (!t62) goto ie56;
        {
            void * t64 = gm_l2;
            gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)t64) + 0));
            uint8_t t66 = 0;
            F.ret = t65;
            goto gm_fret;
        }
        goto ix56;
ie56:;
ix56:;
    }
    {
        gm_t6j6smj7qoctu t67;
        void * t68 = gm_l2;
        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)t68) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t67 + 0, t69);
        void * t70 = gm_l0;
        void * t71 = gm_l2;
        gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t71) + 16));
        gm_b16_8 t73 = gm_f_place_lj97s4p((void *)NULL, cx, NULL, t70, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t67 + 16, t73);
        gm_tabb710d9mq2e t74;
        gm_f_format_1d88j1c((void *)&t74, (void *)NULL, cx, NULL, &t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t75 = t74;
        if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_srco4ljquuk5d87, 51, 12);
        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
        gm_b16_8 t77 = t76;
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_pfpd9oj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_pfpd9oj F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    gm_tl874t2atnlh3 gm_l2; /* x */
    gm_tl874t2atnlh3 gm_l3; /* y */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)t2));
        uint64_t t4 = (uint64_t)t3;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_tir_dump_ppa6e9g *)((&F.h)->up))->l0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
        if (t5 >= (uintptr_t)(t7)) gm_trap_index(t5, t7, gm_srco4ljquuk5d87, 29, 14);
        gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t5 * 136) + 16));
        gm_l2 = t8;
    }
    {
        void * t9 = gm_l1;
        uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)t9));
        uint64_t t11 = (uint64_t)t10;
        uint64_t t12 = (uintptr_t)t11;
        void * t13 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_tir_dump_ppa6e9g *)((&F.h)->up))->l0));
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 344) + 0) + 8);
        if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srco4ljquuk5d87, 30, 14);
        gm_tl874t2atnlh3 t15 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 344) + 0)) + t12 * 136) + 16));
        gm_l3 = t15;
    }
    {
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
        uint8_t t19 = (!(t17 == t18));
        if (!t19) goto ie16;
        {
            uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t23 = (t21 < t22);
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        uint8_t t28 = (!(t26 == t27));
        if (!t28) goto ie25;
        {
            uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            uint8_t t32 = (t30 < t31);
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        goto ix25;
ie25:;
ix25:;
    }
    {
        uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
        uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t37 = (!(t35 == t36));
        if (!t37) goto ie34;
        {
            uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
            uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t41 = (t39 < t40);
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        void * t43 = gm_l0;
        uint32_t t44 = GM_LD(uint32_t, ((uint8_t *)t43));
        void * t45 = gm_l1;
        uint32_t t46 = GM_LD(uint32_t, ((uint8_t *)t45));
        uint8_t t47 = (t44 < t46);
        uint8_t t48 = 0;
        F.ret = t47;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_local_text_75tuacb(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_local_text_75tuacb F;
    void * gm_l0; /* d */
    uint32_t gm_l1; /* l */
    gm_b16_8 gm_l2; /* locals */
    GM_STACK_CHECK(&F, 423u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 48));
        uint64_t t4 = (uint64_t)t3;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 384) + 0) + 8);
        if (t5 >= (uintptr_t)(t8)) gm_trap_index(t5, t8, gm_srco4ljquuk5d87, 106, 15);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 384) + 0)) + t5 * 64) + 24));
        gm_l2 = t9;
    }
    {
        uint32_t t11 = gm_l1;
        uint64_t t12 = (uint64_t)t11;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t14 = (t12 < t13);
        if (!t14) goto ie10;
        {
            gm_tiilmfd16v3fc t16;
            memset((uint8_t *)&t16 + 4, 0, 4);
            uint32_t t17 = gm_l1;
            GM_ST(uint32_t, (uint8_t *)&t16 + 0, t17);
            uint32_t t18 = gm_l1;
            uint64_t t19 = (uint64_t)t18;
            uint64_t t20 = (uintptr_t)t19;
            uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srco4ljquuk5d87, 107, 62);
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t20 * 40) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t16 + 8, t22);
            gm_tabb710d9mq2e t23;
            gm_f_format_fp763nd((void *)&t23, (void *)NULL, cx, NULL, &t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t24 = t23;
            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_srco4ljquuk5d87, 107, 39);
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
            gm_b16_8 t26 = t25;
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        uint32_t t29 = gm_l1;
        gm_t88rcucg1b7uk t30;
        GM_ST(uint32_t, ((uint8_t *)&t30 + 0), t29);
        gm_tabb710d9mq2e t31;
        gm_f_format_irh7ehn((void *)&t31, (void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t32 = t31;
        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_srco4ljquuk5d87, 108, 12);
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
        gm_b16_8 t34 = t33;
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
        uint8_t t36 = 0;
        F.ret = t35;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_place_lj97s4p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_place_lj97s4p F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* file */
    gm_tcc1rq4n1avb2 gm_l3; /* lc */
    GM_STACK_CHECK(&F, 277u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t6 = gm_f_file_o15fqku(t4, t5);
        gm_l2 = t6;
    }
    {
        void * t7 = gm_l2;
        uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_tcc1rq4n1avb2 t9 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t7, t8));
        gm_l3 = t9;
    }
    {
        gm_t5hdnnftkup6m t10;
        void * t11 = gm_l2;
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t11) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t12);
        uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
        GM_ST(uint32_t, (uint8_t *)&t10 + 16, t13);
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        GM_ST(uint32_t, (uint8_t *)&t10 + 20, t14);
        gm_tabb710d9mq2e t15;
        gm_f_format_45lt1qt((void *)&t15, (void *)NULL, cx, NULL, &t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_srco4ljquuk5d87, 58, 12);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
        gm_b16_8 t18 = t17;
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        uint8_t t20 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_rmw_text_te4ol3u(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rmw_text_te4ol3u F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    GM_STACK_CHECK(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; default: goto ma10; }
ma3:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t13 != ((uint8_t)0ULL)) goto mn12;
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_spr274eleu2lj));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(3));
    t1 = t14;
    goto me2;
mn12:;
ma4:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)1ULL)) goto mn15;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_ssigpbgi88it3));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(3));
    t1 = t17;
    goto me2;
mn15:;
ma5:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)2ULL)) goto mn18;
    gm_b16_8 t20;
    GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s4bdmssec9aiu));
    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
    t1 = t20;
    goto me2;
mn18:;
ma6:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t22 != ((uint8_t)3ULL)) goto mn21;
    gm_b16_8 t23;
    GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sfch9mo7lqp8l));
    GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(2));
    t1 = t23;
    goto me2;
mn21:;
ma7:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t25 != ((uint8_t)4ULL)) goto mn24;
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sismsc98l6u4e));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(3));
    t1 = t26;
    goto me2;
mn24:;
ma8:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t28 != ((uint8_t)5ULL)) goto mn27;
    gm_b16_8 t29;
    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sgo00f0mafbnd));
    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(3));
    t1 = t29;
    goto me2;
mn27:;
ma9:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t31 != ((uint8_t)6ULL)) goto mn30;
    gm_b16_8 t32;
    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s187rb18i1kgk));
    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(3));
    t1 = t32;
    goto me2;
mn30:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 581, 44);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sink_text_s4o8tr0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_sink_text_s4o8tr0 F;
    void * gm_l0; /* d */
    gm_tiilmfd16v3fc gm_l2; /* e */
    uint32_t gm_l3; /* op */
    GM_STACK_CHECK(&F, 302u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    gm_tiilmfd16v3fc t5 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)a1) + 8) + 0));
    gm_l2 = t5;
    uint32_t t6 = GM_LD(uint32_t, ((((uint8_t *)a1) + 8) + 24));
    gm_l3 = t6;
    gm_tommf4j9upomc t7;
    memset((uint8_t *)&t7 + 20, 0, 4);
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
    void * t10 = (void *)(((uint8_t *)t9) + 8);
    void * t11 = (void *)((uint8_t *)&gm_l2);
    gm_b16_8 t12 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t7 + 0, t12);
    uint32_t t13 = gm_l3;
    GM_ST(uint32_t, (uint8_t *)&t7 + 16, t13);
    gm_tabb710d9mq2e t14;
    gm_f_format_pvddll3((void *)&t14, (void *)NULL, cx, NULL, &t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t15 = t14;
    if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_srco4ljquuk5d87, 117, 24);
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
    gm_b16_8 t17 = t16;
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
    t1 = t18;
    goto me2;
mn3:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t20 != ((uint8_t)1ULL)) goto mn19;
    gm_b16_8 t21;
    GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s579fb7l5l6df));
    GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(2));
    t1 = t21;
    goto me2;
mn19:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 116, 73);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_op_text_ud90n2l(void *env, gm_node *cx, gm_node *inst, gm_tesaltf0mhcki a0) {
    struct gm_fr_soa_op_text_ud90n2l F;
    gm_tesaltf0mhcki gm_l0; /* op */
    gm_toh0ootodhjue gm_l1; /* x */
    GM_STACK_CHECK(&F, 314u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)3ULL)) goto mn3;
    gm_toh0ootodhjue t5 = GM_LD(gm_toh0ootodhjue, (((uint8_t *)&gm_l0) + 1));
    gm_l1 = t5;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 1));
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 2));
    gm_toh0ootodhjue t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), t6);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 1), t7);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 2), t8);
    gm_tabb710d9mq2e t10;
    gm_f_format_4i3nb5n((void *)&t10, (void *)NULL, cx, NULL, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t11 = t10;
    if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_srco4ljquuk5d87, 586, 18);
    gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
    gm_b16_8 t13 = t12;
    gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
    t1 = t14;
    goto me2;
mn3:;
    gm_b16_8 t16;
    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t27) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma20; case ((uint8_t)3ULL): goto ma21; case ((uint8_t)4ULL): goto ma22; case ((uint8_t)5ULL): goto ma23; case ((uint8_t)6ULL): goto ma24; case ((uint8_t)7ULL): goto ma25; default: goto ma26; }
ma18:;
    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t29 != ((uint8_t)0ULL)) goto mn28;
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s2a9t8smkevg0));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(3));
    t16 = t30;
    goto me17;
mn28:;
ma19:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t32 != ((uint8_t)1ULL)) goto mn31;
    gm_b16_8 t33;
    GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sbgepjj6hgjej));
    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(3));
    t16 = t33;
    goto me17;
mn31:;
ma20:;
    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t35 != ((uint8_t)2ULL)) goto mn34;
    gm_b16_8 t36;
    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s4lt93qhapike));
    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(3));
    t16 = t36;
    goto me17;
mn34:;
ma21:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t38 != ((uint8_t)3ULL)) goto mn37;
    gm_b16_8 t39;
    GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sqa0b48i8mvk7));
    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(5));
    t16 = t39;
    goto me17;
mn37:;
ma22:;
    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t41 != ((uint8_t)4ULL)) goto mn40;
    gm_b16_8 t42;
    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s35gfshei80n5));
    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(7));
    t16 = t42;
    goto me17;
mn40:;
ma23:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t44 != ((uint8_t)5ULL)) goto mn43;
    gm_b16_8 t45;
    GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s3o5gaeteeggu));
    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(4));
    t16 = t45;
    goto me17;
mn43:;
ma24:;
    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t47 != ((uint8_t)6ULL)) goto mn46;
    gm_b16_8 t48;
    GM_ST(void *, (uint8_t *)&t48, (void *)(gm_sah1jmr01nidj));
    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(4));
    t16 = t48;
    goto me17;
mn46:;
ma25:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t50 != ((uint8_t)7ULL)) goto mn49;
    gm_b16_8 t51;
    GM_ST(void *, (uint8_t *)&t51, (void *)(gm_supvu1ruutbov));
    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(8));
    t16 = t51;
    goto me17;
mn49:;
ma26:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 587, 10);
me17:;
    t1 = t16;
    goto me2;
mn15:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 585, 59);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_spec_text_fr9884d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_spec_text_fr9884d F;
    void * gm_l0; /* spec */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* align */
    gm_b16_8 gm_l3; /* kind */
    uint32_t gm_l4; /* p */
    GM_STACK_CHECK(&F, 501u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 1) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; default: goto ma9; }
ma5:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 1) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sbqq5iijjahf9));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(4));
        t3 = t13;
        goto me4;
mn11:;
ma6:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 1) + 0));
        if (t15 != ((uint8_t)1ULL)) goto mn14;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s5n5e484glelv));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(4));
        t3 = t16;
        goto me4;
mn14:;
ma7:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 1) + 0));
        if (t18 != ((uint8_t)2ULL)) goto mn17;
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s6edba3m7mc9a));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(5));
        t3 = t19;
        goto me4;
mn17:;
ma8:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 1) + 0));
        if (t21 != ((uint8_t)3ULL)) goto mn20;
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s2ma5cp41jk1a));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(6));
        t3 = t22;
        goto me4;
mn20:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 430, 14);
me4:;
        gm_l2 = t3;
    }
    {
        void * t23 = gm_l0;
        gm_b16_8 t24;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        switch (t34) { case ((uint8_t)0ULL): goto ma26; case ((uint8_t)1ULL): goto ma27; case ((uint8_t)2ULL): goto ma28; case ((uint8_t)3ULL): goto ma29; case ((uint8_t)4ULL): goto ma30; case ((uint8_t)5ULL): goto ma31; case ((uint8_t)6ULL): goto ma32; default: goto ma33; }
ma26:;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        if (t36 != ((uint8_t)0ULL)) goto mn35;
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sc5qopgd7dm66));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(7));
        t24 = t37;
        goto me25;
mn35:;
ma27:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        if (t39 != ((uint8_t)1ULL)) goto mn38;
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sbsb3nmbfgbv2));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(3));
        t24 = t40;
        goto me25;
mn38:;
ma28:;
        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        if (t42 != ((uint8_t)2ULL)) goto mn41;
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s85jtgs7au5b5));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(8));
        t24 = t43;
        goto me25;
mn41:;
ma29:;
        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        if (t45 != ((uint8_t)3ULL)) goto mn44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s49o1oiv7k0po));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(3));
        t24 = t46;
        goto me25;
mn44:;
ma30:;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        if (t48 != ((uint8_t)4ULL)) goto mn47;
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_seg9l1usqbs3j));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(3));
        t24 = t49;
        goto me25;
mn47:;
ma31:;
        uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        if (t51 != ((uint8_t)5ULL)) goto mn50;
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sgfufhs3psibd));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(3));
        t24 = t52;
        goto me25;
mn50:;
ma32:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        if (t54 != ((uint8_t)6ULL)) goto mn53;
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sajbccb18c45d));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(5));
        t24 = t55;
        goto me25;
mn53:;
ma33:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 436, 13);
me25:;
        gm_l3 = t24;
    }
    {
        void * t56 = gm_l1;
        gm_th32vq80aukjr t57;
        memset((uint8_t *)&t57 + 1, 0, 7);
        memset((uint8_t *)&t57 + 27, 0, 1);
        void * t58 = gm_l0;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)t58) + 0));
        GM_ST(uint8_t, (uint8_t *)&t57 + 0, t59);
        gm_b16_8 t60 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t57 + 8, t60);
        void * t61 = gm_l0;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)t61) + 2));
        GM_ST(uint8_t, (uint8_t *)&t57 + 24, t62);
        void * t63 = gm_l0;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t63) + 3));
        GM_ST(uint8_t, (uint8_t *)&t57 + 25, t64);
        void * t65 = gm_l0;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t65) + 4));
        GM_ST(uint8_t, (uint8_t *)&t57 + 26, t66);
        void * t67 = gm_l0;
        uint32_t t68 = GM_LD(uint32_t, (((uint8_t *)t67) + 8));
        GM_ST(uint32_t, (uint8_t *)&t57 + 28, t68);
        gm_tftb1odc739t5 t69 = gm_f_write_t5i5r03(t56, &t57);
        gm_tftb1odc739t5 t70 = t69;
        if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_srco4ljquuk5d87, 445, 5);
        gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
    }
    {
        void * t73 = gm_l0;
        if (!(((((uint8_t *)t73) + 12))[4] != 0)) goto ie72;
        uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)t73) + 12));
        gm_l4 = t74;
        {
            void * t76 = gm_l1;
            uint32_t t77 = gm_l4;
            gm_t88rcucg1b7uk t78;
            GM_ST(uint32_t, ((uint8_t *)&t78 + 0), t77);
            gm_tftb1odc739t5 t79 = gm_f_write_7h6pth0(t76, t78);
            gm_tftb1odc739t5 t80 = t79;
            if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_srco4ljquuk5d87, 447, 9);
            gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
        }
        goto ix72;
ie72:;
        {
            void * t83 = gm_l1;
            gm_b16_8 t84;
            GM_ST(void *, (uint8_t *)&t84, (void *)(gm_s026u6q948evn));
            GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(4));
            gm_tftb1odc739t5 t85 = gm_f_extend_0cs85gs(t83, t84);
            gm_tftb1odc739t5 t86 = t85;
            if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srco4ljquuk5d87, 449, 9);
            gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
        }
ix72:;
    }
    {
        void * t88 = gm_l1;
        gm_b16_8 t89 = gm_l3;
        gm_tnpifv20irnph t90;
        GM_ST(gm_b16_8, ((uint8_t *)&t90 + 0), t89);
        gm_tftb1odc739t5 t91 = gm_f_write_7ilko9q(t88, t90);
        gm_tftb1odc739t5 t92 = t91;
        if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srco4ljquuk5d87, 451, 5);
        gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_text_9qjnpra(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_text_9qjnpra F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    GM_STACK_CHECK(&F, 985u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t59) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma19; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma23; case ((uint8_t)21ULL): goto ma24; case ((uint8_t)22ULL): goto ma25; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma27; case ((uint8_t)25ULL): goto ma28; case ((uint8_t)26ULL): goto ma29; case ((uint8_t)27ULL): goto ma30; case ((uint8_t)28ULL): goto ma31; case ((uint8_t)29ULL): goto ma32; case ((uint8_t)30ULL): goto ma33; case ((uint8_t)31ULL): goto ma34; case ((uint8_t)32ULL): goto ma35; case ((uint8_t)33ULL): goto ma36; case ((uint8_t)34ULL): goto ma37; case ((uint8_t)35ULL): goto ma38; case ((uint8_t)36ULL): goto ma39; case ((uint8_t)37ULL): goto ma40; case ((uint8_t)38ULL): goto ma41; case ((uint8_t)39ULL): goto ma42; case ((uint8_t)40ULL): goto ma43; case ((uint8_t)41ULL): goto ma44; case ((uint8_t)42ULL): goto ma45; case ((uint8_t)43ULL): goto ma46; case ((uint8_t)44ULL): goto ma47; case ((uint8_t)45ULL): goto ma48; case ((uint8_t)46ULL): goto ma49; case ((uint8_t)47ULL): goto ma50; case ((uint8_t)48ULL): goto ma51; case ((uint8_t)49ULL): goto ma52; case ((uint8_t)50ULL): goto ma53; case ((uint8_t)51ULL): goto ma54; case ((uint8_t)52ULL): goto ma55; case ((uint8_t)53ULL): goto ma56; case ((uint8_t)54ULL): goto ma57; default: goto ma58; }
ma3:;
    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t61 != ((uint8_t)0ULL)) goto mn60;
    gm_b16_8 t62;
    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s93umj3p0tala));
    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(4));
    t1 = t62;
    goto me2;
mn60:;
ma4:;
    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t64 != ((uint8_t)1ULL)) goto mn63;
    gm_b16_8 t65;
    GM_ST(void *, (uint8_t *)&t65, (void *)(gm_slnhe1h36jmub));
    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(8));
    t1 = t65;
    goto me2;
mn63:;
ma5:;
    uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t67 != ((uint8_t)2ULL)) goto mn66;
    gm_b16_8 t68;
    GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sa8dse4jk5ibv));
    GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(9));
    t1 = t68;
    goto me2;
mn66:;
ma6:;
    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t70 != ((uint8_t)3ULL)) goto mn69;
    gm_b16_8 t71;
    GM_ST(void *, (uint8_t *)&t71, (void *)(gm_smmus0krp2svr));
    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(5));
    t1 = t71;
    goto me2;
mn69:;
ma7:;
    uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t73 != ((uint8_t)4ULL)) goto mn72;
    gm_b16_8 t74;
    GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sa5s0jools6a2));
    GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(8));
    t1 = t74;
    goto me2;
mn72:;
ma8:;
    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t76 != ((uint8_t)5ULL)) goto mn75;
    gm_b16_8 t77;
    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sacua9qc75rts));
    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(6));
    t1 = t77;
    goto me2;
mn75:;
ma9:;
    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t79 != ((uint8_t)6ULL)) goto mn78;
    gm_b16_8 t80;
    GM_ST(void *, (uint8_t *)&t80, (void *)(gm_siqgrmkn8n4vn));
    GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(7));
    t1 = t80;
    goto me2;
mn78:;
ma10:;
    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t82 != ((uint8_t)7ULL)) goto mn81;
    gm_b16_8 t83;
    GM_ST(void *, (uint8_t *)&t83, (void *)(gm_sc03pp1oj3q4k));
    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(6));
    t1 = t83;
    goto me2;
mn81:;
ma11:;
    uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t85 != ((uint8_t)8ULL)) goto mn84;
    gm_b16_8 t86;
    GM_ST(void *, (uint8_t *)&t86, (void *)(gm_s9i2ld3a42lap));
    GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(7));
    t1 = t86;
    goto me2;
mn84:;
ma12:;
    uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t88 != ((uint8_t)9ULL)) goto mn87;
    gm_b16_8 t89;
    GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sjbhp9l7seful));
    GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(6));
    t1 = t89;
    goto me2;
mn87:;
ma13:;
    uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t91 != ((uint8_t)10ULL)) goto mn90;
    gm_b16_8 t92;
    GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sit8u7uppaik9));
    GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(6));
    t1 = t92;
    goto me2;
mn90:;
ma14:;
    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t94 != ((uint8_t)11ULL)) goto mn93;
    gm_b16_8 t95;
    GM_ST(void *, (uint8_t *)&t95, (void *)(gm_sf5llhuhdlajp));
    GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(8));
    t1 = t95;
    goto me2;
mn93:;
ma15:;
    uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t97 != ((uint8_t)12ULL)) goto mn96;
    gm_b16_8 t98;
    GM_ST(void *, (uint8_t *)&t98, (void *)(gm_so8fkrgrobcsk));
    GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(8));
    t1 = t98;
    goto me2;
mn96:;
ma16:;
    uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t100 != ((uint8_t)13ULL)) goto mn99;
    gm_b16_8 t101;
    GM_ST(void *, (uint8_t *)&t101, (void *)(gm_so3oq96gr0k5r));
    GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(9));
    t1 = t101;
    goto me2;
mn99:;
ma17:;
    uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t103 != ((uint8_t)14ULL)) goto mn102;
    gm_b16_8 t104;
    GM_ST(void *, (uint8_t *)&t104, (void *)(gm_snstimc3n04u0));
    GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(11));
    t1 = t104;
    goto me2;
mn102:;
ma18:;
    uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t106 != ((uint8_t)15ULL)) goto mn105;
    gm_b16_8 t107;
    GM_ST(void *, (uint8_t *)&t107, (void *)(gm_shvp36midgf58));
    GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(9));
    t1 = t107;
    goto me2;
mn105:;
ma19:;
    uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t109 != ((uint8_t)16ULL)) goto mn108;
    gm_b16_8 t110;
    GM_ST(void *, (uint8_t *)&t110, (void *)(gm_slov3heeajhqn));
    GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(11));
    t1 = t110;
    goto me2;
mn108:;
ma20:;
    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t112 != ((uint8_t)17ULL)) goto mn111;
    gm_b16_8 t113;
    GM_ST(void *, (uint8_t *)&t113, (void *)(gm_segr993uh2crm));
    GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(10));
    t1 = t113;
    goto me2;
mn111:;
ma21:;
    uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t115 != ((uint8_t)18ULL)) goto mn114;
    gm_b16_8 t116;
    GM_ST(void *, (uint8_t *)&t116, (void *)(gm_s3ol5q9br79vf));
    GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(7));
    t1 = t116;
    goto me2;
mn114:;
ma22:;
    uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t118 != ((uint8_t)19ULL)) goto mn117;
    gm_b16_8 t119;
    GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sk721elh8ouv2));
    GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(10));
    t1 = t119;
    goto me2;
mn117:;
ma23:;
    uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t121 != ((uint8_t)20ULL)) goto mn120;
    gm_b16_8 t122;
    GM_ST(void *, (uint8_t *)&t122, (void *)(gm_s6nm27ehg597q));
    GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(6));
    t1 = t122;
    goto me2;
mn120:;
ma24:;
    uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t124 != ((uint8_t)21ULL)) goto mn123;
    gm_b16_8 t125;
    GM_ST(void *, (uint8_t *)&t125, (void *)(gm_stehcnejvoo24));
    GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(5));
    t1 = t125;
    goto me2;
mn123:;
ma25:;
    uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t127 != ((uint8_t)22ULL)) goto mn126;
    gm_b16_8 t128;
    GM_ST(void *, (uint8_t *)&t128, (void *)(gm_s6ofccoi3ursa));
    GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(11));
    t1 = t128;
    goto me2;
mn126:;
ma26:;
    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t130 != ((uint8_t)23ULL)) goto mn129;
    gm_b16_8 t131;
    GM_ST(void *, (uint8_t *)&t131, (void *)(gm_stidgejoe9ul2));
    GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(10));
    t1 = t131;
    goto me2;
mn129:;
ma27:;
    uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t133 != ((uint8_t)24ULL)) goto mn132;
    gm_b16_8 t134;
    GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sai3vjkob4s2v));
    GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(9));
    t1 = t134;
    goto me2;
mn132:;
ma28:;
    uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t136 != ((uint8_t)25ULL)) goto mn135;
    gm_b16_8 t137;
    GM_ST(void *, (uint8_t *)&t137, (void *)(gm_sgtriqnjufjbf));
    GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(9));
    t1 = t137;
    goto me2;
mn135:;
ma29:;
    uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t139 != ((uint8_t)26ULL)) goto mn138;
    gm_b16_8 t140;
    GM_ST(void *, (uint8_t *)&t140, (void *)(gm_smgioupbmjajr));
    GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(13));
    t1 = t140;
    goto me2;
mn138:;
ma30:;
    uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t142 != ((uint8_t)27ULL)) goto mn141;
    gm_b16_8 t143;
    GM_ST(void *, (uint8_t *)&t143, (void *)(gm_s1js4dn5e65ja));
    GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(10));
    t1 = t143;
    goto me2;
mn141:;
ma31:;
    uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t145 != ((uint8_t)28ULL)) goto mn144;
    gm_b16_8 t146;
    GM_ST(void *, (uint8_t *)&t146, (void *)(gm_s5e0seq0he7ou));
    GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(9));
    t1 = t146;
    goto me2;
mn144:;
ma32:;
    uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t148 != ((uint8_t)29ULL)) goto mn147;
    gm_b16_8 t149;
    GM_ST(void *, (uint8_t *)&t149, (void *)(gm_so2sjlodq81qn));
    GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(12));
    t1 = t149;
    goto me2;
mn147:;
ma33:;
    uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t151 != ((uint8_t)30ULL)) goto mn150;
    gm_b16_8 t152;
    GM_ST(void *, (uint8_t *)&t152, (void *)(gm_spskhsf5tnedj));
    GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(9));
    t1 = t152;
    goto me2;
mn150:;
ma34:;
    uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t154 != ((uint8_t)31ULL)) goto mn153;
    gm_b16_8 t155;
    GM_ST(void *, (uint8_t *)&t155, (void *)(gm_sqlk0h0kae56e));
    GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(7));
    t1 = t155;
    goto me2;
mn153:;
ma35:;
    uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t157 != ((uint8_t)32ULL)) goto mn156;
    gm_b16_8 t158;
    GM_ST(void *, (uint8_t *)&t158, (void *)(gm_s4gilalcmdvu3));
    GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(7));
    t1 = t158;
    goto me2;
mn156:;
ma36:;
    uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t160 != ((uint8_t)33ULL)) goto mn159;
    gm_b16_8 t161;
    GM_ST(void *, (uint8_t *)&t161, (void *)(gm_sjfcbh429vmot));
    GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(8));
    t1 = t161;
    goto me2;
mn159:;
ma37:;
    uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t163 != ((uint8_t)34ULL)) goto mn162;
    gm_b16_8 t164;
    GM_ST(void *, (uint8_t *)&t164, (void *)(gm_soerl6rds8b9v));
    GM_ST(uintptr_t, (uint8_t *)&t164 + 8, (uintptr_t)(11));
    t1 = t164;
    goto me2;
mn162:;
ma38:;
    uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t166 != ((uint8_t)35ULL)) goto mn165;
    gm_b16_8 t167;
    GM_ST(void *, (uint8_t *)&t167, (void *)(gm_s7m3rbk4l87ah));
    GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(14));
    t1 = t167;
    goto me2;
mn165:;
ma39:;
    uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t169 != ((uint8_t)36ULL)) goto mn168;
    gm_b16_8 t170;
    GM_ST(void *, (uint8_t *)&t170, (void *)(gm_sm0gs6tv176le));
    GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(7));
    t1 = t170;
    goto me2;
mn168:;
ma40:;
    uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t172 != ((uint8_t)37ULL)) goto mn171;
    gm_b16_8 t173;
    GM_ST(void *, (uint8_t *)&t173, (void *)(gm_s1gf327eapo1j));
    GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(10));
    t1 = t173;
    goto me2;
mn171:;
ma41:;
    uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t175 != ((uint8_t)38ULL)) goto mn174;
    gm_b16_8 t176;
    GM_ST(void *, (uint8_t *)&t176, (void *)(gm_sgm565ktbj9ua));
    GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(11));
    t1 = t176;
    goto me2;
mn174:;
ma42:;
    uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t178 != ((uint8_t)39ULL)) goto mn177;
    gm_b16_8 t179;
    GM_ST(void *, (uint8_t *)&t179, (void *)(gm_sjdrosvte680q));
    GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(11));
    t1 = t179;
    goto me2;
mn177:;
ma43:;
    uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t181 != ((uint8_t)40ULL)) goto mn180;
    gm_b16_8 t182;
    GM_ST(void *, (uint8_t *)&t182, (void *)(gm_stdefoi5sk91c));
    GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(11));
    t1 = t182;
    goto me2;
mn180:;
ma44:;
    uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t184 != ((uint8_t)41ULL)) goto mn183;
    gm_b16_8 t185;
    GM_ST(void *, (uint8_t *)&t185, (void *)(gm_so1u9tqjfpsmc));
    GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)(8));
    t1 = t185;
    goto me2;
mn183:;
ma45:;
    uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t187 != ((uint8_t)42ULL)) goto mn186;
    gm_b16_8 t188;
    GM_ST(void *, (uint8_t *)&t188, (void *)(gm_s19069udlvhni));
    GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(8));
    t1 = t188;
    goto me2;
mn186:;
ma46:;
    uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t190 != ((uint8_t)43ULL)) goto mn189;
    gm_b16_8 t191;
    GM_ST(void *, (uint8_t *)&t191, (void *)(gm_s2289h15tkpi1));
    GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(11));
    t1 = t191;
    goto me2;
mn189:;
ma47:;
    uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t193 != ((uint8_t)44ULL)) goto mn192;
    gm_b16_8 t194;
    GM_ST(void *, (uint8_t *)&t194, (void *)(gm_sqhkrul9t68sa));
    GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(7));
    t1 = t194;
    goto me2;
mn192:;
ma48:;
    uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t196 != ((uint8_t)45ULL)) goto mn195;
    gm_b16_8 t197;
    GM_ST(void *, (uint8_t *)&t197, (void *)(gm_shtuc6se4lilq));
    GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(6));
    t1 = t197;
    goto me2;
mn195:;
ma49:;
    uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t199 != ((uint8_t)46ULL)) goto mn198;
    gm_b16_8 t200;
    GM_ST(void *, (uint8_t *)&t200, (void *)(gm_s5k0vuc678fh1));
    GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(7));
    t1 = t200;
    goto me2;
mn198:;
ma50:;
    uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t202 != ((uint8_t)47ULL)) goto mn201;
    gm_b16_8 t203;
    GM_ST(void *, (uint8_t *)&t203, (void *)(gm_sm017vofcj044));
    GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(11));
    t1 = t203;
    goto me2;
mn201:;
ma51:;
    uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t205 != ((uint8_t)48ULL)) goto mn204;
    gm_b16_8 t206;
    GM_ST(void *, (uint8_t *)&t206, (void *)(gm_s7d4tu5fkkg12));
    GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(10));
    t1 = t206;
    goto me2;
mn204:;
ma52:;
    uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t208 != ((uint8_t)49ULL)) goto mn207;
    gm_b16_8 t209;
    GM_ST(void *, (uint8_t *)&t209, (void *)(gm_ss308e7joejdo));
    GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(12));
    t1 = t209;
    goto me2;
mn207:;
ma53:;
    uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t211 != ((uint8_t)50ULL)) goto mn210;
    gm_b16_8 t212;
    GM_ST(void *, (uint8_t *)&t212, (void *)(gm_ss6dev5u7tl99));
    GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(11));
    t1 = t212;
    goto me2;
mn210:;
ma54:;
    uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t214 != ((uint8_t)51ULL)) goto mn213;
    gm_b16_8 t215;
    GM_ST(void *, (uint8_t *)&t215, (void *)(gm_sakmuvnjjp217));
    GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(9));
    t1 = t215;
    goto me2;
mn213:;
ma55:;
    uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t217 != ((uint8_t)52ULL)) goto mn216;
    gm_b16_8 t218;
    GM_ST(void *, (uint8_t *)&t218, (void *)(gm_sln5qir3tgi7j));
    GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(9));
    t1 = t218;
    goto me2;
mn216:;
ma56:;
    uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t220 != ((uint8_t)53ULL)) goto mn219;
    gm_b16_8 t221;
    GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sqfu5kl4vat80));
    GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(8));
    t1 = t221;
    goto me2;
mn219:;
ma57:;
    uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t223 != ((uint8_t)54ULL)) goto mn222;
    gm_b16_8 t224;
    GM_ST(void *, (uint8_t *)&t224, (void *)(gm_sbcq15b9em6fm));
    GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(8));
    t1 = t224;
    goto me2;
mn222:;
ma58:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 583, 44);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_target_text_4kebb7t(void *env, gm_node *cx, gm_node *inst, gm_te9hrk0u35rev a0) {
    struct gm_fr_target_text_4kebb7t F;
    gm_te9hrk0u35rev gm_l0; /* t */
    uint32_t gm_l1; /* levels */
    uint32_t gm_l2; /* label */
    GM_STACK_CHECK(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    uint32_t t5 = GM_LD(uint32_t, ((((uint8_t *)&gm_l0) + 4) + 0));
    gm_l1 = t5;
    uint32_t t6 = GM_LD(uint32_t, ((((uint8_t *)&gm_l0) + 4) + 4));
    gm_l2 = t6;
    uint32_t t7 = gm_l1;
    uint32_t t8 = gm_l2;
    gm_tcc1rq4n1avb2 t9;
    GM_ST(uint32_t, ((uint8_t *)&t9 + 0), t7);
    GM_ST(uint32_t, ((uint8_t *)&t9 + 4), t8);
    gm_tabb710d9mq2e t10;
    gm_f_format_kj3qi5h((void *)&t10, (void *)NULL, cx, NULL, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t11 = t10;
    if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_srco4ljquuk5d87, 112, 30);
    gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
    gm_b16_8 t13 = t12;
    gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
    t1 = t14;
    goto me2;
mn3:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)1ULL)) goto mn15;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_srri8c6al1v1g));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
    t1 = t17;
    goto me2;
mn15:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 111, 59);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_tir_dump_ppa6e9g(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_tir_dump_ppa6e9g F;
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l5; /* sig */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* f */
    GM_STACK_CHECK(&F, 577u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_f_list_go2qg0b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = F.l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 344) + 0) + 8);
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
                        uint64_t t17 = gm_l4;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = F.l0;
                        uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 344) + 0) + 8);
                        if (t18 >= (uintptr_t)(t20)) gm_trap_index(t18, t20, gm_srco4ljquuk5d87, 23, 17);
                        void * t21 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 344) + 0)) + t18 * 136);
                        gm_l5 = t21;
                    }
                    {
                        uint8_t t23 = 0;
                        uint8_t t24 = 0;
                        void * t25 = gm_l5;
                        gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t25) + 16));
                        gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                        gm_tl874t2atnlh3 t28 = t26;
                        gm_tl874t2atnlh3 t29 = t27;
                        uint8_t t30 = (gm_eqff85ac10o3(((uint8_t *)&t28), ((uint8_t *)&t29)));
                        if (!t30) {
                            void * t31 = gm_l5;
                            uint8_t t32 = (((GM_LD(uintptr_t, (((uint8_t *)t31) + 0) + 8) == 7) && memcmp(GM_LD(void *, (((uint8_t *)t31) + 0)), gm_stgqtu1f7bco8, 7) == 0));
                            t24 = t32;
                        } else t24 = 1;
                        if (!t24) {
                            void * t33 = gm_l5;
                            uint8_t t34;
                            if ((((((uint8_t *)t33) + 96))[24] != 0)) goto mn36;
                            t34 = ((uint8_t)0);
                            goto me35;
mn36:;
                            t34 = ((uint8_t)1);
                            goto me35;
mn37:;
                            gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 24, 65);
me35:;
                            t23 = t34;
                        } else t23 = 1;
                        if (!t23) goto ie22;
                        {
                            goto lc5;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    {
                        void * t40 = F.l0;
                        void * t41 = GM_LD(void *, (((uint8_t *)t40) + 0));
                        void * t42 = GM_LD(void *, (((uint8_t *)t41) + 0));
                        void * t43 = gm_l5;
                        uint32_t t44 = GM_LD(uint32_t, ((((uint8_t *)t43) + 16) + 0));
                        void * t45 = gm_f_file_o15fqku(t42, t44);
                        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)t45) + 0));
                        gm_b16_8 t47;
                        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_slmii6hjigrcr));
                        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(6));
                        uint8_t t48 = gm_f_starts_with_6vvt01e(t46, t47);
                        if (!t48) goto ie39;
                        {
                            goto lc5;
                        }
                        goto ix39;
ie39:;
ix39:;
                    }
                    {
                        void * t50 = (void *)((uint8_t *)&F.l1);
                        uint64_t t51 = gm_l4;
                        uint32_t t52 = (uint32_t)t51;
                        gm_tftb1odc739t5 t53 = gm_f_push_im9d4ro(t50, t52);
                        gm_tftb1odc739t5 t54 = t53;
                        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srco4ljquuk5d87, 26, 9);
                        gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 22, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t57 = gm_l3;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l3 = t60;
                }
            }
lb5:;
        }
    }
    {
        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_closure t62 = ((gm_closure){(void *)gm_f_fn_pfpd9oj, (void *)&F.h, cx});
        gm_f_sort_by_egbtonb(t61, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memset((uint8_t *)&F.l6 + 52, 0, 4);
        void * t63 = F.l0;
        GM_ST(void *, (uint8_t *)&F.l6 + 0, t63);
        gm_f_list_uvfu1k1((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l6 + 8)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&F.l6 + 48, ((uint32_t)0ULL));
    }
    {
        {
            gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            gm_l7 = t65;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t67 = gm_l8;
                uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t69 = (t67 < t68);
                if (!t69) goto lb66;
                {
                    uint64_t t72 = gm_l8;
                    uint64_t t73 = (uintptr_t)t72;
                    uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint32_t t75 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t73 * 4));
                    uint32_t t76 = t75;
                    uint32_t t79 = GM_LD(uint32_t, ((uint8_t *)&t76));
                    gm_l9 = t79;
                    {
                        void * t81 = (void *)((uint8_t *)&F.l6);
                        uint32_t t82 = gm_l9;
                        gm_b0_1 t83 = gm_f_write_fn_jkakt7l((void *)NULL, cx, NULL, t81, t82);
                        if (gm_unw.kind) {
                            goto gm_ll70;
                        }
                    }
                    goto me77;
mn78:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 37, 5);
me77:;
                }
lc66:;
                {
                    uint64_t t85 = gm_l8;
                    uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                    uint64_t t88 = r86;
                    gm_l8 = t88;
                }
            }
gm_ll70:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb66; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc66; }
            }
            goto gm_lad;
lb66:;
        }
    }
    {
        gm_b16_8 t89 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 8) + 0));
        gm_b16_8 t90 = t89;
        gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
        uint8_t t92 = 0;
        F.ret = t91;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_arm_4vimkqo(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_write_arm_4vimkqo F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    void * gm_l2; /* a */
    void * gm_l3; /* g */
    GM_STACK_CHECK(&F, 248u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s76ud0alkjfse));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
        gm_b0_1 t5 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        uint64_t t7 = gm_l1;
        uint64_t r8; bool o9 = __builtin_add_overflow(t7, ((uint64_t)1ULL), &r8);
        if (o9) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 518, 17);
        uint64_t t10 = r8;
        void * t11 = gm_l2;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        gm_b0_1 t13 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t6, t10, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t15 = gm_l2;
        void * t16 = GM_LD(void *, (((uint8_t *)t15) + 8));
        void * t17 = t16;
        if (!(GM_LD(void *, ((uint8_t *)&t17)) != 0)) goto ie14;
        void * t18 = GM_LD(void *, ((uint8_t *)&t17));
        gm_l3 = t18;
        {
            void * t20 = gm_l0;
            uint64_t t21 = gm_l1;
            uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
            if (o23) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 520, 22);
            uint64_t t24 = r22;
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sqflguscq1uc5));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(5));
            gm_b0_1 t26 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t20, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t27 = gm_l0;
            uint64_t t28 = gm_l1;
            uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)2ULL), &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 521, 22);
            uint64_t t31 = r29;
            void * t32 = gm_l3;
            gm_b0_1 t33 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t27, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t34 = gm_l0;
        uint64_t t35 = gm_l1;
        uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
        if (o37) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 523, 18);
        uint64_t t38 = r36;
        void * t39 = gm_l2;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 16));
        gm_b0_1 t41 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t34, t38, t40);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_block_q5mpqjf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_write_block_q5mpqjf F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    void * gm_l2; /* b */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_trcmmd12cefb1 gm_l5; /* st */
    uint32_t gm_l6; /* l */
    uint32_t gm_l7; /* l */
    void * gm_l8; /* x */
    gm_b16_8 gm_l9; /* ls */
    void * gm_l10; /* x */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_t9805qtonbsag gm_l14; /* l */
    uint32_t gm_l15; /* id */
    void * gm_l16; /* p */
    void * gm_l17; /* v */
    gm_b16_8 gm_l18; /* ps */
    void * gm_l19; /* v */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    void * gm_l22; /* p */
    void * gm_l23; /* x */
    void * gm_l24; /* x */
    uint8_t gm_l25; /* is_err */
    void * gm_l26; /* x */
    void * gm_l27; /* t */
    GM_STACK_CHECK(&F, 1942u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l2;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l3 = t4;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_trcmmd12cefb1 t16 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 32));
                    gm_l5 = t16;
                    {
                        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        switch (t27) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma23; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma25; default: goto ma26; }
ma19:;
                        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t29 != ((uint8_t)0ULL)) goto mn28;
                        uint32_t t30 = GM_LD(uint32_t, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l6 = t30;
                        if ((GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8)) != 0)) goto mn28;
                        void * t31 = gm_l0;
                        uint64_t t32 = gm_l1;
                        void * t33 = gm_l0;
                        uint32_t t34 = gm_l6;
                        gm_b16_8 t35 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t33, t34);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tnpifv20irnph t36;
                        GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t35);
                        gm_tabb710d9mq2e t37;
                        gm_f_format_cqr3tc0((void *)&t37, (void *)NULL, cx, NULL, t36);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tabb710d9mq2e t38 = t37;
                        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 8, gm_srco4ljquuk5d87, 457, 50);
                        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 8));
                        gm_b16_8 t40 = t39;
                        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                        gm_b0_1 t42 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t31, t32, t41);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        goto me18;
mn28:;
ma20:;
                        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t44 != ((uint8_t)0ULL)) goto mn43;
                        uint32_t t45 = GM_LD(uint32_t, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l7 = t45;
                        if (!(GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8)) != 0)) goto mn43;
                        void * t46 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                        gm_l8 = t46;
                        {
                            void * t48 = gm_l0;
                            uint64_t t49 = gm_l1;
                            void * t50 = gm_l0;
                            uint32_t t51 = gm_l7;
                            gm_b16_8 t52 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t50, t51);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tnpifv20irnph t53;
                            GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
                            gm_tabb710d9mq2e t54;
                            gm_f_format_cqr3tc0((void *)&t54, (void *)NULL, cx, NULL, t53);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tabb710d9mq2e t55 = t54;
                            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_srco4ljquuk5d87, 459, 37);
                            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
                            gm_b16_8 t57 = t56;
                            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                            gm_b0_1 t59 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t48, t49, t58);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t60 = gm_l0;
                            uint64_t t61 = gm_l1;
                            uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                            if (o63) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 460, 30);
                            uint64_t t64 = r62;
                            void * t65 = gm_l8;
                            gm_b0_1 t66 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t60, t64, t65);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn43:;
ma21:;
                        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t68 != ((uint8_t)1ULL)) goto mn67;
                        gm_b16_8 t69 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l9 = t69;
                        void * t70 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 16));
                        gm_l10 = t70;
                        {
                            gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            {
                                gm_b16_8 t73 = gm_l9;
                                gm_l12 = t73;
                            }
                            {
                                gm_l13 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t75 = gm_l13;
                                    uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                    uint8_t t77 = (t75 < t76);
                                    if (!t77) goto lb74;
                                    {
                                        uint64_t t80 = gm_l13;
                                        uint64_t t81 = (uintptr_t)t80;
                                        uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                        gm_t9805qtonbsag t85 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t81 * 8));
                                        gm_l14 = t85;
                                        {
                                            void * t87 = (void *)((uint8_t *)&F.l11);
                                            gm_b16_8 t88;
                                            if (!((((uint8_t *)&gm_l14))[4] != 0)) goto ie89;
                                            uint32_t t90 = GM_LD(uint32_t, ((uint8_t *)&gm_l14));
                                            gm_l15 = t90;
                                            void * t92 = gm_l0;
                                            uint32_t t93 = gm_l15;
                                            gm_b16_8 t94 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t92, t93);
                                            if (gm_unw.kind) {
                                                goto gm_ll78;
                                            }
                                            gm_b16_8 t95 = t94;
                                            t88 = t95;
                                            goto ix89;
ie89:;
                                            gm_b16_8 t97;
                                            GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sfdbfrv46lihd));
                                            GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(1));
                                            gm_b16_8 t98 = t97;
                                            t88 = t98;
ix89:;
                                            gm_tftb1odc739t5 t99 = gm_f_push_3ef746b(t87, t88);
                                            gm_tftb1odc739t5 t100 = t99;
                                            if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_srco4ljquuk5d87, 464, 31);
                                            gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
                                        }
                                        goto me83;
mn84:;
                                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 464, 17);
me83:;
                                    }
lc74:;
                                    {
                                        uint64_t t103 = gm_l13;
                                        uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                                        uint64_t t106 = r104;
                                        gm_l13 = t106;
                                    }
                                }
gm_ll78:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb74; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc74; }
                                }
                                goto gm_ll9;
lb74:;
                            }
                        }
                        {
                            void * t107 = gm_l0;
                            uint64_t t108 = gm_l1;
                            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                            gm_b16_8 t110 = t109;
                            gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                            gm_b16_8 t112;
                            GM_ST(void *, (uint8_t *)&t112, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(2));
                            gm_tabb710d9mq2e t113;
                            gm_f_join_sfknljb((void *)&t113, (void *)NULL, cx, NULL, t111, t112);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tabb710d9mq2e t114 = t113;
                            if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 8, gm_srco4ljquuk5d87, 465, 64);
                            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
                            gm_tnpifv20irnph t116;
                            GM_ST(gm_b16_8, ((uint8_t *)&t116 + 0), t115);
                            gm_tabb710d9mq2e t117;
                            gm_f_format_qhad3qp((void *)&t117, (void *)NULL, cx, NULL, t116);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tabb710d9mq2e t118 = t117;
                            if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 8, gm_srco4ljquuk5d87, 465, 37);
                            gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&t118) + 8));
                            gm_b16_8 t120 = t119;
                            gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                            gm_b0_1 t122 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t107, t108, t121);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t123 = gm_l0;
                            uint64_t t124 = gm_l1;
                            uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                            if (o126) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 466, 30);
                            uint64_t t127 = r125;
                            void * t128 = gm_l10;
                            gm_b0_1 t129 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t123, t127, t128);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn67:;
ma22:;
                        uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t131 != ((uint8_t)2ULL)) goto mn130;
                        void * t132 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l16 = t132;
                        void * t133 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                        gm_l17 = t133;
                        {
                            void * t135 = gm_l0;
                            uint64_t t136 = gm_l1;
                            gm_b16_8 t137;
                            GM_ST(void *, (uint8_t *)&t137, (void *)(gm_sugq7dl55ocon));
                            GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(6));
                            gm_b0_1 t138 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t135, t136, t137);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t139 = gm_l0;
                            uint64_t t140 = gm_l1;
                            uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
                            if (o142) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 470, 30);
                            uint64_t t143 = r141;
                            void * t144 = gm_l16;
                            gm_b0_1 t145 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t139, t143, t144);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t146 = gm_l0;
                            uint64_t t147 = gm_l1;
                            uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
                            if (o149) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 471, 30);
                            uint64_t t150 = r148;
                            void * t151 = gm_l17;
                            gm_b0_1 t152 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t146, t150, t151);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn130:;
ma23:;
                        uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t154 != ((uint8_t)3ULL)) goto mn153;
                        gm_b16_8 t155 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l18 = t155;
                        void * t156 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 16));
                        gm_l19 = t156;
                        {
                            void * t158 = gm_l0;
                            uint64_t t159 = gm_l1;
                            gm_b16_8 t160;
                            GM_ST(void *, (uint8_t *)&t160, (void *)(gm_s0ve0dba4rtj8));
                            GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(12));
                            gm_b0_1 t161 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t158, t159, t160);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            {
                                gm_b16_8 t163 = gm_l18;
                                gm_l20 = t163;
                            }
                            {
                                gm_l21 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t165 = gm_l21;
                                    uint64_t t166 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                    uint8_t t167 = (t165 < t166);
                                    if (!t167) goto lb164;
                                    {
                                        uint64_t t170 = gm_l21;
                                        uint64_t t171 = (uintptr_t)t170;
                                        uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                        void * t173 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t171 * 8));
                                        void * t174 = t173;
                                        void * t177 = GM_LD(void *, ((uint8_t *)&t174));
                                        gm_l22 = t177;
                                        {
                                            void * t180 = gm_l22;
                                            void * t181 = t180;
                                            if (!(GM_LD(void *, ((uint8_t *)&t181)) != 0)) goto ie179;
                                            void * t182 = GM_LD(void *, ((uint8_t *)&t181));
                                            gm_l23 = t182;
                                            {
                                                void * t184 = gm_l0;
                                                uint64_t t185 = gm_l1;
                                                uint64_t r186; bool o187 = __builtin_add_overflow(t185, ((uint64_t)1ULL), &r186);
                                                if (o187) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 477, 38);
                                                uint64_t t188 = r186;
                                                void * t189 = gm_l23;
                                                gm_b0_1 t190 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t184, t188, t189);
                                                if (gm_unw.kind) {
                                                    goto gm_ll168;
                                                }
                                            }
                                            goto ix179;
ie179:;
                                            {
                                                void * t192 = gm_l0;
                                                uint64_t t193 = gm_l1;
                                                uint64_t r194; bool o195 = __builtin_add_overflow(t193, ((uint64_t)1ULL), &r194);
                                                if (o195) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 479, 38);
                                                uint64_t t196 = r194;
                                                gm_b16_8 t197;
                                                GM_ST(void *, (uint8_t *)&t197, (void *)(gm_sfdbfrv46lihd));
                                                GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(1));
                                                gm_b0_1 t198 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t192, t196, t197);
                                                if (gm_unw.kind) {
                                                    goto gm_ll168;
                                                }
                                            }
ix179:;
                                        }
                                        goto me175;
mn176:;
                                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 475, 17);
me175:;
                                    }
lc164:;
                                    {
                                        uint64_t t200 = gm_l21;
                                        uint64_t r201; bool o202 = __builtin_add_overflow(t200, ((uint64_t)1ULL), &r201);
                                        uint64_t t203 = r201;
                                        gm_l21 = t203;
                                    }
                                }
gm_ll168:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb164; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc164; }
                                }
                                goto gm_ll9;
lb164:;
                            }
                        }
                        {
                            void * t204 = gm_l0;
                            uint64_t t205 = gm_l1;
                            uint64_t r206; bool o207 = __builtin_add_overflow(t205, ((uint64_t)1ULL), &r206);
                            if (o207) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 482, 30);
                            uint64_t t208 = r206;
                            void * t209 = gm_l19;
                            gm_b0_1 t210 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t204, t208, t209);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn153:;
ma24:;
                        uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t212 != ((uint8_t)4ULL)) goto mn211;
                        void * t213 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                        gm_l24 = t213;
                        {
                            void * t215 = gm_l0;
                            uint64_t t216 = gm_l1;
                            gm_b16_8 t217;
                            GM_ST(void *, (uint8_t *)&t217, (void *)(gm_sk5s72mtujop8));
                            GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(4));
                            gm_b0_1 t218 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t215, t216, t217);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t219 = gm_l0;
                            uint64_t t220 = gm_l1;
                            uint64_t r221; bool o222 = __builtin_add_overflow(t220, ((uint64_t)1ULL), &r221);
                            if (o222) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 486, 30);
                            uint64_t t223 = r221;
                            void * t224 = gm_l24;
                            gm_b0_1 t225 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t219, t223, t224);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn211:;
ma25:;
                        uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t227 != ((uint8_t)5ULL)) goto mn226;
                        uint8_t t228 = GM_LD(uint8_t, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l25 = t228;
                        void * t229 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                        gm_l26 = t229;
                        {
                            void * t231 = gm_l0;
                            uint64_t t232 = gm_l1;
                            gm_b16_8 t233;
                            uint8_t t235 = gm_l25;
                            if (!t235) goto ie234;
                            gm_b16_8 t237;
                            GM_ST(void *, (uint8_t *)&t237, (void *)(gm_ser8ip7vtbv6c));
                            GM_ST(uintptr_t, (uint8_t *)&t237 + 8, (uintptr_t)(8));
                            gm_b16_8 t238 = t237;
                            t233 = t238;
                            goto ix234;
ie234:;
                            gm_b16_8 t240;
                            GM_ST(void *, (uint8_t *)&t240, (void *)(gm_sllcm1pqagh28));
                            GM_ST(uintptr_t, (uint8_t *)&t240 + 8, (uintptr_t)(5));
                            gm_b16_8 t241 = t240;
                            t233 = t241;
ix234:;
                            gm_b0_1 t242 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t231, t232, t233);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t243 = gm_l0;
                            uint64_t t244 = gm_l1;
                            uint64_t r245; bool o246 = __builtin_add_overflow(t244, ((uint64_t)1ULL), &r245);
                            if (o246) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 490, 30);
                            uint64_t t247 = r245;
                            void * t248 = gm_l26;
                            gm_b0_1 t249 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t243, t247, t248);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn226:;
ma26:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 456, 9);
me18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 455, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t251 = gm_l4;
                    uint64_t r252; bool o253 = __builtin_add_overflow(t251, ((uint64_t)1ULL), &r252);
                    uint64_t t254 = r252;
                    gm_l4 = t254;
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
    {
        void * t256 = gm_l2;
        void * t257 = GM_LD(void *, (((uint8_t *)t256) + 16));
        void * t258 = t257;
        if (!(GM_LD(void *, ((uint8_t *)&t258)) != 0)) goto ie255;
        void * t259 = GM_LD(void *, ((uint8_t *)&t258));
        gm_l27 = t259;
        {
            void * t261 = gm_l0;
            uint64_t t262 = gm_l1;
            gm_b16_8 t263;
            GM_ST(void *, (uint8_t *)&t263, (void *)(gm_skl3b78lo1gln));
            GM_ST(uintptr_t, (uint8_t *)&t263 + 8, (uintptr_t)(4));
            gm_b0_1 t264 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t261, t262, t263);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t265 = gm_l0;
            uint64_t t266 = gm_l1;
            uint64_t r267; bool o268 = __builtin_add_overflow(t266, ((uint64_t)1ULL), &r267);
            if (o268) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 496, 22);
            uint64_t t269 = r267;
            void * t270 = gm_l27;
            gm_b0_1 t271 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t265, t269, t270);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix255;
ie255:;
ix255:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_conds_k9e57un(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_write_conds_k9e57un F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_b16_8 gm_l2; /* cond */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tlr9dp305htm7 gm_l5; /* c */
    void * gm_l6; /* x */
    void * gm_l7; /* p */
    void * gm_l8; /* v */
    GM_STACK_CHECK(&F, 387u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l2;
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
                    gm_tlr9dp305htm7 t15 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 24));
                    gm_l5 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        void * t20 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                        gm_l6 = t20;
                        {
                            void * t22 = gm_l0;
                            uint64_t t23 = gm_l1;
                            gm_b16_8 t24;
                            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sobcm12sogdng));
                            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(4));
                            gm_b0_1 t25 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t22, t23, t24);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t26 = gm_l0;
                            uint64_t t27 = gm_l1;
                            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                            if (o29) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 505, 30);
                            uint64_t t30 = r28;
                            void * t31 = gm_l6;
                            gm_b0_1 t32 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t26, t30, t31);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto me17;
mn18:;
                        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t34 != ((uint8_t)1ULL)) goto mn33;
                        void * t35 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l7 = t35;
                        void * t36 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                        gm_l8 = t36;
                        {
                            void * t38 = gm_l0;
                            uint64_t t39 = gm_l1;
                            gm_b16_8 t40;
                            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sfid9en449upv));
                            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(10));
                            gm_b0_1 t41 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t38, t39, t40);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t42 = gm_l0;
                            uint64_t t43 = gm_l1;
                            uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                            if (o45) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 509, 29);
                            uint64_t t46 = r44;
                            void * t47 = gm_l7;
                            gm_b0_1 t48 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t42, t46, t47);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t49 = gm_l0;
                            uint64_t t50 = gm_l1;
                            uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                            if (o52) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 510, 30);
                            uint64_t t53 = r51;
                            void * t54 = gm_l8;
                            gm_b0_1 t55 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t49, t53, t54);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto me17;
mn33:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 502, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 501, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t57 = gm_l4;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l4 = t60;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_expr_cftucor(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_write_expr_cftucor F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    void * gm_l2; /* e */
    void * gm_l3; /* s */
    gm_b16_8 gm_l4; /* ty */
    double gm_l7; /* x */
    gm_tandvbt63uk7n gm_l9; /* x */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    uint8_t gm_l14; /* b */
    gm_b16_8 gm_l15; /* b */
    gm_b16_8 gm_l16; /* b */
    uint32_t gm_l17; /* p */
    gm_t88rcucg1b7uk gm_l18; /* of */
    uint8_t gm_l19; /* align */
    gm_t88rcucg1b7uk gm_l20; /* of */
    uint8_t gm_l21; /* max */
    gm_t88rcucg1b7uk gm_l22; /* of */
    gm_t88rcucg1b7uk gm_l23; /* a */
    gm_t88rcucg1b7uk gm_l24; /* b */
    uint8_t gm_l25; /* negated */
    gm_b16_8 gm_l26; /* name */
    gm_tl874t2atnlh3 gm_l27; /* span */
    uint32_t gm_l28; /* l */
    gm_b16_8 gm_l29; /* name */
    uint32_t gm_l30; /* levels */
    uint32_t gm_l31; /* l */
    void * gm_l32; /* x */
    void * gm_l33; /* base */
    uint32_t gm_l34; /* index */
    void * gm_l35; /* base */
    void * gm_l36; /* index */
    uint8_t gm_l37; /* checked */
    gm_tao63pk3r18v5 gm_l38; /* sl */
    void * gm_l39; /* x */
    void * gm_l40; /* x */
    void * gm_l41; /* x */
    void * gm_l42; /* x */
    void * gm_l43; /* x */
    void * gm_l44; /* x */
    uint32_t gm_l45; /* f */
    uint32_t gm_l46; /* f */
    uint32_t gm_l47; /* h */
    gm_b16_8 gm_l50; /* <tmp> */
    uint64_t gm_l51; /* <tmp> */
    uint32_t gm_l52; /* c */
    gm_t9qkh0rsooc8d gm_l54; /* op */
    void * gm_l55; /* x */
    gm_ts6oqppjh0ba3 gm_l56; /* op */
    void * gm_l57; /* a */
    void * gm_l58; /* b */
    uint8_t gm_l59; /* is_and */
    void * gm_l60; /* a */
    void * gm_l61; /* b */
    gm_tmn7n0n9raou5 gm_l62; /* kind */
    void * gm_l63; /* x */
    gm_tvdg94bk7aa22 gm_l64; /* op */
    gm_b16_8 gm_l65; /* args */
    uint64_t gm_l66; /* <tmp> */
    uint64_t gm_l67; /* <tmp> */
    uint64_t gm_l68; /* i */
    gm_b16_8 gm_l70; /* args */
    uint32_t gm_l71; /* f */
    gm_tiilmfd16v3fc gm_l72; /* eff */
    uint32_t gm_l73; /* op */
    void * gm_l74; /* c */
    gm_b16_8 gm_l75; /* h */
    uint64_t gm_l76; /* <tmp> */
    uint64_t gm_l77; /* <tmp> */
    uint64_t gm_l78; /* i */
    gm_b16_8 gm_l79; /* xs */
    uint64_t gm_l80; /* <tmp> */
    uint64_t gm_l81; /* <tmp> */
    uint64_t gm_l82; /* i */
    gm_b16_8 gm_l83; /* xs */
    uint64_t gm_l84; /* <tmp> */
    uint64_t gm_l85; /* <tmp> */
    uint64_t gm_l86; /* i */
    void * gm_l87; /* x */
    gm_ttggq7ng4kp3p gm_l88; /* v */
    void * gm_l89; /* p */
    void * gm_l90; /* x */
    gm_t3dcoc7077t1q gm_l91; /* x */
    uint32_t gm_l92; /* c */
    gm_t083kd7b1kkag gm_l93; /* x */
    uint32_t gm_l94; /* v */
    void * gm_l95; /* x */
    uint8_t gm_l96; /* is_result */
    void * gm_l97; /* a */
    void * gm_l98; /* b */
    uint8_t gm_l99; /* negated */
    void * gm_l100; /* a */
    void * gm_l101; /* b */
    uint8_t gm_l102; /* negated */
    void * gm_l103; /* scrutinee */
    gm_b16_8 gm_l104; /* arms */
    gm_b16_8 gm_l105; /* <tmp> */
    uint64_t gm_l106; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l107; /* a */
    void * gm_l108; /* b */
    gm_trl2khmuphncd gm_l109; /* x */
    gm_teo9d4mncjfdu gm_l111; /* x */
    void * gm_l112; /* v */
    gm_te9hrk0u35rev gm_l113; /* t */
    uint32_t gm_l114; /* levels */
    void * gm_l115; /* v */
    void * gm_l116; /* x */
    void * gm_l117; /* k */
    void * gm_l118; /* v */
    void * gm_l119; /* k */
    void * gm_l121; /* x */
    uint64_t gm_l122; /* <tmp> */
    uint64_t gm_l123; /* <tmp> */
    uint64_t gm_l124; /* i */
    void * gm_l126; /* x */
    void * gm_l127; /* a */
    gm_b16_8 gm_l128; /* head */
    gm_b16_8 gm_l129; /* <tmp> */
    uint64_t gm_l130; /* <tmp> */
    void * gm_l131; /* k */
    GM_STACK_CHECK(&F, 12071u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l3;
        void * t5 = gm_l2;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 0));
        gm_b16_8 t7 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t4, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t7;
    }
    {
        gm_f_list_s1kpkpr((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t8 = gm_l2;
        gm_b16_8 t9;
        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        switch (t71) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma16; case ((uint8_t)5ULL): goto ma17; case ((uint8_t)6ULL): goto ma18; case ((uint8_t)7ULL): goto ma19; case ((uint8_t)8ULL): goto ma20; case ((uint8_t)9ULL): goto ma21; case ((uint8_t)10ULL): goto ma22; case ((uint8_t)11ULL): goto ma23; case ((uint8_t)12ULL): goto ma24; case ((uint8_t)13ULL): goto ma25; case ((uint8_t)14ULL): goto ma26; case ((uint8_t)15ULL): goto ma27; case ((uint8_t)16ULL): goto ma29; case ((uint8_t)17ULL): goto ma28; case ((uint8_t)18ULL): goto ma30; case ((uint8_t)19ULL): goto ma31; case ((uint8_t)20ULL): goto ma32; case ((uint8_t)21ULL): goto ma33; case ((uint8_t)22ULL): goto ma34; case ((uint8_t)23ULL): goto ma35; case ((uint8_t)24ULL): goto ma36; case ((uint8_t)25ULL): goto ma37; case ((uint8_t)26ULL): goto ma38; case ((uint8_t)27ULL): goto ma39; case ((uint8_t)28ULL): goto ma40; case ((uint8_t)29ULL): goto ma42; case ((uint8_t)30ULL): goto ma43; case ((uint8_t)31ULL): goto ma44; case ((uint8_t)32ULL): goto ma45; case ((uint8_t)33ULL): goto ma46; case ((uint8_t)34ULL): goto ma47; case ((uint8_t)35ULL): goto ma48; case ((uint8_t)36ULL): goto ma49; case ((uint8_t)37ULL): goto ma50; case ((uint8_t)38ULL): goto ma51; case ((uint8_t)39ULL): goto ma52; case ((uint8_t)40ULL): goto ma55; case ((uint8_t)41ULL): goto ma53; case ((uint8_t)42ULL): goto ma54; case ((uint8_t)43ULL): goto ma56; case ((uint8_t)44ULL): goto ma58; case ((uint8_t)45ULL): goto ma57; case ((uint8_t)46ULL): goto ma59; case ((uint8_t)47ULL): goto ma60; case ((uint8_t)48ULL): goto ma61; case ((uint8_t)49ULL): goto ma62; case ((uint8_t)50ULL): goto ma63; case ((uint8_t)51ULL): goto ma64; case ((uint8_t)52ULL): goto ma65; case ((uint8_t)53ULL): goto ma66; case ((uint8_t)54ULL): goto ma67; case ((uint8_t)55ULL): goto ma68; case ((uint8_t)56ULL): goto ma69; case ((uint8_t)57ULL): goto ma14; case ((uint8_t)58ULL): goto ma41; default: goto ma70; }
ma11:;
        uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t73 != ((uint8_t)0ULL)) goto mn72;
        gm_tbci5ospk5nnn t74 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t8) + 8) + 8));
        F.l6 = t74;
        gm_b16_8 t75 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t76;
        GM_ST(gm_b16_8, ((uint8_t *)&t76 + 0), t75);
        gm_tabb710d9mq2e t77;
        gm_f_format_ejli3qf((void *)&t77, (void *)NULL, cx, NULL, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t78 = t77;
        if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 8, gm_srco4ljquuk5d87, 126, 20);
        gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t78) + 8));
        gm_b16_8 t80 = t79;
        gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
        t9 = t81;
        goto me10;
mn72:;
ma12:;
        uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t83 != ((uint8_t)1ULL)) goto mn82;
        double t84 = GM_LD(double, ((((uint8_t *)t8) + 8) + 8));
        gm_l7 = t84;
        double t85 = gm_l7;
        double t86 = t85;
        uint64_t t87;
        memset(&t87, 0, sizeof t87); memcpy(&t87, ((uint8_t *)&t86), sizeof t87 < 8 ? sizeof t87 : 8);
        gm_t4ce0jd3tdro4 t88;
        GM_ST(uint64_t, ((uint8_t *)&t88 + 0), t87);
        gm_tabb710d9mq2e t89;
        gm_f_format_booci4l((void *)&t89, (void *)NULL, cx, NULL, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t90 = t89;
        if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 8, gm_srco4ljquuk5d87, 127, 22);
        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&t90) + 8));
        gm_b16_8 t92 = t91;
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
        t9 = t93;
        goto me10;
mn82:;
ma13:;
        uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t95 != ((uint8_t)2ULL)) goto mn94;
        gm_tngtuiuskn938 t96 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)t8) + 8) + 8));
        F.l8 = t96;
        gm_b16_8 t97 = gm_f_show_rat_1s624db((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t98;
        GM_ST(gm_b16_8, ((uint8_t *)&t98 + 0), t97);
        gm_tabb710d9mq2e t99;
        gm_f_format_nva8q4r((void *)&t99, (void *)NULL, cx, NULL, t98);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t100 = t99;
        if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 8, gm_srco4ljquuk5d87, 128, 29);
        gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&t100) + 8));
        gm_b16_8 t102 = t101;
        gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
        t9 = t103;
        goto me10;
mn94:;
ma14:;
        uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t105 != ((uint8_t)57ULL)) goto mn104;
        gm_tandvbt63uk7n t106 = GM_LD(gm_tandvbt63uk7n, ((((uint8_t *)t8) + 8) + 8));
        gm_l9 = t106;
        {
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                uint64_t t109 = GM_LD(uintptr_t, (((uint8_t *)&gm_l9) + 8) + 8);
                gm_l10 = t109;
            }
            {
                for (;;) {
                    uint64_t t111 = gm_l11;
                    uint64_t t112 = gm_l10;
                    uint8_t t113 = (t111 < t112);
                    if (!t113) goto lb110;
                    {
                        uint64_t t116 = gm_l11;
                        uint64_t t117 = t116;
                        uint64_t t120 = GM_LD(uint64_t, ((uint8_t *)&t117));
                        gm_l12 = t120;
                        {
                            void * t122 = (void *)((uint8_t *)&F.l5);
                            uint64_t t123 = gm_l12;
                            uint64_t t124 = (uintptr_t)t123;
                            uint64_t t125 = GM_LD(uintptr_t, (((uint8_t *)&gm_l9) + 8) + 8);
                            if (t124 >= (uintptr_t)(t125)) gm_trap_index(t124, t125, gm_srco4ljquuk5d87, 130, 49);
                            void * t126 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l9) + 8)) + t124 * 224);
                            gm_tftb1odc739t5 t127 = gm_f_push_pf02nv6(t122, t126);
                            gm_tftb1odc739t5 t128 = t127;
                            if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 1, gm_srco4ljquuk5d87, 130, 38);
                            gm_b0_1 t129 = GM_LD(gm_b0_1, (((uint8_t *)&t128) + 1));
                        }
                        goto me118;
mn119:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 130, 13);
me118:;
                    }
lc110:;
                    {
                        uint64_t t131 = gm_l11;
                        uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
                        uint64_t t134 = r132;
                        gm_l11 = t134;
                    }
                }
lb110:;
            }
        }
        gm_tesaltf0mhcki t135 = GM_LD(gm_tesaltf0mhcki, (((uint8_t *)&gm_l9) + 0));
        gm_b16_8 t136 = gm_f_soa_op_text_ud90n2l((void *)NULL, cx, NULL, t135);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t137;
        GM_ST(gm_b16_8, ((uint8_t *)&t137 + 0), t136);
        gm_tabb710d9mq2e t138;
        gm_f_format_t0vntpb((void *)&t138, (void *)NULL, cx, NULL, t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t139 = t138;
        if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 8, gm_srco4ljquuk5d87, 131, 13);
        gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&t139) + 8));
        gm_b16_8 t141 = t140;
        gm_b16_8 t142 = t141;
        gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
        t9 = t143;
        goto me10;
mn104:;
ma15:;
        uint8_t t145 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t145 != ((uint8_t)3ULL)) goto mn144;
        gm_tu15p0hi2qlv6 t146 = GM_LD(gm_tu15p0hi2qlv6, ((((uint8_t *)t8) + 8) + 8));
        F.l13 = t146;
        gm_t6j6smj7qoctu t147;
        gm_b16_8 t148 = gm_f_show_rat_1s624db((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)(((uint8_t *)&F.l13) + 0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t147 + 0, t148);
        gm_b16_8 t149 = gm_f_show_rat_1s624db((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)(((uint8_t *)&F.l13) + 96)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t147 + 16, t149);
        gm_tabb710d9mq2e t150;
        gm_f_format_0969kd8((void *)&t150, (void *)NULL, cx, NULL, &t147);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t151 = t150;
        if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 8, gm_srco4ljquuk5d87, 133, 31);
        gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&t151) + 8));
        gm_b16_8 t153 = t152;
        gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&t153));
        t9 = t154;
        goto me10;
mn144:;
ma16:;
        uint8_t t156 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t156 != ((uint8_t)4ULL)) goto mn155;
        uint8_t t157 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 8));
        gm_l14 = t157;
        uint8_t t158 = gm_l14;
        gm_t9qkh0rsooc8d t159;
        GM_ST(uint8_t, ((uint8_t *)&t159 + 0), t158);
        gm_tabb710d9mq2e t160;
        gm_f_format_as0b1t3((void *)&t160, (void *)NULL, cx, NULL, t159);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t161 = t160;
        if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 8, gm_srco4ljquuk5d87, 134, 21);
        gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&t161) + 8));
        gm_b16_8 t163 = t162;
        gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)&t163));
        t9 = t164;
        goto me10;
mn155:;
ma17:;
        uint8_t t166 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t166 != ((uint8_t)5ULL)) goto mn165;
        gm_b16_8 t167 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 8) + 8));
        gm_l15 = t167;
        gm_b16_8 t168;
        GM_ST(void *, (uint8_t *)&t168, (void *)(gm_shdnhcpvptc1q));
        GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(4));
        gm_b16_8 t169 = gm_l15;
        gm_b16_8 t170 = gm_f_escaped_k5fm4jl((void *)NULL, cx, NULL, t168, t169);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t9 = t170;
        goto me10;
mn165:;
ma18:;
        uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t172 != ((uint8_t)6ULL)) goto mn171;
        gm_b16_8 t173 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 8) + 8));
        gm_l16 = t173;
        gm_b16_8 t174;
        GM_ST(void *, (uint8_t *)&t174, (void *)(gm_sdak257j1qgl8));
        GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(5));
        gm_b16_8 t175 = gm_l16;
        gm_b16_8 t176 = gm_f_escaped_k5fm4jl((void *)NULL, cx, NULL, t174, t175);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t9 = t176;
        goto me10;
mn171:;
ma19:;
        uint8_t t178 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t178 != ((uint8_t)7ULL)) goto mn177;
        gm_b16_8 t179;
        GM_ST(void *, (uint8_t *)&t179, (void *)(gm_squedv51820an));
        GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(4));
        t9 = t179;
        goto me10;
mn177:;
ma20:;
        uint8_t t181 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t181 != ((uint8_t)8ULL)) goto mn180;
        gm_b16_8 t182;
        GM_ST(void *, (uint8_t *)&t182, (void *)(gm_sr899mamktpvo));
        GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(6));
        t9 = t182;
        goto me10;
mn180:;
ma21:;
        uint8_t t184 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t184 != ((uint8_t)9ULL)) goto mn183;
        uint32_t t185 = GM_LD(uint32_t, ((((uint8_t *)t8) + 8) + 8));
        gm_l17 = t185;
        void * t186 = gm_l0;
        void * t187 = GM_LD(void *, (((uint8_t *)t186) + 0));
        void * t188 = (void *)(((uint8_t *)t187) + 8);
        uint32_t t189 = gm_l17;
        gm_b16_8 t190 = gm_f_param_name_rbnop5l(t188, t189);
        gm_tnpifv20irnph t191;
        GM_ST(gm_b16_8, ((uint8_t *)&t191 + 0), t190);
        gm_tabb710d9mq2e t192;
        gm_f_format_r9ba7g9((void *)&t192, (void *)NULL, cx, NULL, t191);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t193 = t192;
        if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 8, gm_srco4ljquuk5d87, 139, 27);
        gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&t193) + 8));
        gm_b16_8 t195 = t194;
        gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&t195));
        t9 = t196;
        goto me10;
mn183:;
ma22:;
        uint8_t t198 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t198 != ((uint8_t)10ULL)) goto mn197;
        gm_t88rcucg1b7uk t199 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l18 = t199;
        uint8_t t200 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 4));
        gm_l19 = t200;
        gm_t6j6smj7qoctu t201;
        gm_b16_8 t202;
        uint8_t t204 = gm_l19;
        if (!t204) goto ie203;
        gm_b16_8 t206;
        GM_ST(void *, (uint8_t *)&t206, (void *)(gm_s8gtcifve79ai));
        GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(8));
        gm_b16_8 t207 = t206;
        t202 = t207;
        goto ix203;
ie203:;
        gm_b16_8 t209;
        GM_ST(void *, (uint8_t *)&t209, (void *)(gm_sjegrp8u9r3n3));
        GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(7));
        gm_b16_8 t210 = t209;
        t202 = t210;
ix203:;
        GM_ST(gm_b16_8, (uint8_t *)&t201 + 0, t202);
        void * t211 = gm_l3;
        gm_t88rcucg1b7uk t212 = gm_l18;
        gm_b16_8 t213 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t211, t212);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t201 + 16, t213);
        gm_tabb710d9mq2e t214;
        gm_f_format_2lgoflc((void *)&t214, (void *)NULL, cx, NULL, &t201);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t215 = t214;
        if (((((uint8_t *)&t215))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t215) + 8, gm_srco4ljquuk5d87, 140, 31);
        gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&t215) + 8));
        gm_b16_8 t217 = t216;
        gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&t217));
        t9 = t218;
        goto me10;
mn197:;
ma23:;
        uint8_t t220 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t220 != ((uint8_t)11ULL)) goto mn219;
        gm_t88rcucg1b7uk t221 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l20 = t221;
        uint8_t t222 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 4));
        gm_l21 = t222;
        gm_t6j6smj7qoctu t223;
        gm_b16_8 t224;
        uint8_t t226 = gm_l21;
        if (!t226) goto ie225;
        gm_b16_8 t228;
        GM_ST(void *, (uint8_t *)&t228, (void *)(gm_s0lh54534p3vd));
        GM_ST(uintptr_t, (uint8_t *)&t228 + 8, (uintptr_t)(9));
        gm_b16_8 t229 = t228;
        t224 = t229;
        goto ix225;
ie225:;
        gm_b16_8 t231;
        GM_ST(void *, (uint8_t *)&t231, (void *)(gm_sp2smaof0i4j2));
        GM_ST(uintptr_t, (uint8_t *)&t231 + 8, (uintptr_t)(9));
        gm_b16_8 t232 = t231;
        t224 = t232;
ix225:;
        GM_ST(gm_b16_8, (uint8_t *)&t223 + 0, t224);
        void * t233 = gm_l3;
        gm_t88rcucg1b7uk t234 = gm_l20;
        gm_b16_8 t235 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t233, t234);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t223 + 16, t235);
        gm_tabb710d9mq2e t236;
        gm_f_format_2lgoflc((void *)&t236, (void *)NULL, cx, NULL, &t223);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t237 = t236;
        if (((((uint8_t *)&t237))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t237) + 8, gm_srco4ljquuk5d87, 141, 31);
        gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&t237) + 8));
        gm_b16_8 t239 = t238;
        gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)&t239));
        t9 = t240;
        goto me10;
mn219:;
ma24:;
        uint8_t t242 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t242 != ((uint8_t)12ULL)) goto mn241;
        gm_t88rcucg1b7uk t243 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t8) + 8) + 8));
        gm_l22 = t243;
        void * t244 = gm_l3;
        gm_t88rcucg1b7uk t245 = gm_l22;
        gm_b16_8 t246 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t244, t245);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t247;
        GM_ST(gm_b16_8, ((uint8_t *)&t247 + 0), t246);
        gm_tabb710d9mq2e t248;
        gm_f_format_t725p24((void *)&t248, (void *)NULL, cx, NULL, t247);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t249 = t248;
        if (((((uint8_t *)&t249))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t249) + 8, gm_srco4ljquuk5d87, 142, 26);
        gm_b16_8 t250 = GM_LD(gm_b16_8, (((uint8_t *)&t249) + 8));
        gm_b16_8 t251 = t250;
        gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&t251));
        t9 = t252;
        goto me10;
mn241:;
ma25:;
        uint8_t t254 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t254 != ((uint8_t)13ULL)) goto mn253;
        gm_t88rcucg1b7uk t255 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l23 = t255;
        gm_t88rcucg1b7uk t256 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t8) + 8) + 8) + 4));
        gm_l24 = t256;
        uint8_t t257 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l25 = t257;
        gm_tfeab279c16rr t258;
        gm_b16_8 t259;
        uint8_t t261 = gm_l25;
        if (!t261) goto ie260;
        gm_b16_8 t263;
        GM_ST(void *, (uint8_t *)&t263, (void *)(gm_sngdu4l8vh3l8));
        GM_ST(uintptr_t, (uint8_t *)&t263 + 8, (uintptr_t)(4));
        gm_b16_8 t264 = t263;
        t259 = t264;
        goto ix260;
ie260:;
        gm_b16_8 t266;
        GM_ST(void *, (uint8_t *)&t266, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t266 + 8, (uintptr_t)(0));
        gm_b16_8 t267 = t266;
        t259 = t267;
ix260:;
        GM_ST(gm_b16_8, (uint8_t *)&t258 + 0, t259);
        void * t268 = gm_l3;
        gm_t88rcucg1b7uk t269 = gm_l23;
        gm_b16_8 t270 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t268, t269);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t258 + 16, t270);
        void * t271 = gm_l3;
        gm_t88rcucg1b7uk t272 = gm_l24;
        gm_b16_8 t273 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t271, t272);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t258 + 32, t273);
        gm_tabb710d9mq2e t274;
        gm_f_format_arnesr1((void *)&t274, (void *)NULL, cx, NULL, &t258);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t275 = t274;
        if (((((uint8_t *)&t275))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t275) + 8, gm_srco4ljquuk5d87, 143, 35);
        gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)&t275) + 8));
        gm_b16_8 t277 = t276;
        gm_b16_8 t278 = GM_LD(gm_b16_8, ((uint8_t *)&t277));
        t9 = t278;
        goto me10;
mn253:;
ma26:;
        uint8_t t280 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t280 != ((uint8_t)14ULL)) goto mn279;
        gm_b16_8 t281 = GM_LD(gm_b16_8, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l26 = t281;
        gm_tl874t2atnlh3 t282 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l27 = t282;
        gm_t6j6smj7qoctu t283;
        gm_b16_8 t284 = gm_l26;
        GM_ST(gm_b16_8, (uint8_t *)&t283 + 0, t284);
        void * t285 = gm_l3;
        gm_tl874t2atnlh3 t286 = gm_l27;
        gm_b16_8 t287 = gm_f_place_lj97s4p((void *)NULL, cx, NULL, t285, t286);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t283 + 16, t287);
        gm_tabb710d9mq2e t288;
        gm_f_format_5up8eqa((void *)&t288, (void *)NULL, cx, NULL, &t283);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t289 = t288;
        if (((((uint8_t *)&t289))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t289) + 8, gm_srco4ljquuk5d87, 144, 31);
        gm_b16_8 t290 = GM_LD(gm_b16_8, (((uint8_t *)&t289) + 8));
        gm_b16_8 t291 = t290;
        gm_b16_8 t292 = GM_LD(gm_b16_8, ((uint8_t *)&t291));
        t9 = t292;
        goto me10;
mn279:;
ma27:;
        uint8_t t294 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t294 != ((uint8_t)15ULL)) goto mn293;
        uint32_t t295 = GM_LD(uint32_t, ((((uint8_t *)t8) + 8) + 8));
        gm_l28 = t295;
        void * t296 = gm_l0;
        uint32_t t297 = gm_l28;
        gm_b16_8 t298 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t296, t297);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t299;
        GM_ST(gm_b16_8, ((uint8_t *)&t299 + 0), t298);
        gm_tabb710d9mq2e t300;
        gm_f_format_32bp3b4((void *)&t300, (void *)NULL, cx, NULL, t299);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t301 = t300;
        if (((((uint8_t *)&t301))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t301) + 8, gm_srco4ljquuk5d87, 145, 22);
        gm_b16_8 t302 = GM_LD(gm_b16_8, (((uint8_t *)&t301) + 8));
        gm_b16_8 t303 = t302;
        gm_b16_8 t304 = GM_LD(gm_b16_8, ((uint8_t *)&t303));
        t9 = t304;
        goto me10;
mn293:;
ma28:;
        uint8_t t306 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t306 != ((uint8_t)17ULL)) goto mn305;
        gm_b16_8 t307 = GM_LD(gm_b16_8, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l29 = t307;
        gm_b16_8 t308 = gm_l29;
        gm_tnpifv20irnph t309;
        GM_ST(gm_b16_8, ((uint8_t *)&t309 + 0), t308);
        gm_tabb710d9mq2e t310;
        gm_f_format_fi775k0((void *)&t310, (void *)NULL, cx, NULL, t309);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t311 = t310;
        if (((((uint8_t *)&t311))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t311) + 8, gm_srco4ljquuk5d87, 146, 29);
        gm_b16_8 t312 = GM_LD(gm_b16_8, (((uint8_t *)&t311) + 8));
        gm_b16_8 t313 = t312;
        gm_b16_8 t314 = GM_LD(gm_b16_8, ((uint8_t *)&t313));
        t9 = t314;
        goto me10;
mn305:;
ma29:;
        uint8_t t316 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t316 != ((uint8_t)16ULL)) goto mn315;
        uint32_t t317 = GM_LD(uint32_t, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l30 = t317;
        uint32_t t318 = GM_LD(uint32_t, (((((uint8_t *)t8) + 8) + 8) + 4));
        gm_l31 = t318;
        uint32_t t319 = gm_l30;
        uint32_t t320 = gm_l31;
        gm_tcc1rq4n1avb2 t321;
        GM_ST(uint32_t, ((uint8_t *)&t321 + 0), t319);
        GM_ST(uint32_t, ((uint8_t *)&t321 + 4), t320);
        gm_tabb710d9mq2e t322;
        gm_f_format_16dq22e((void *)&t322, (void *)NULL, cx, NULL, t321);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t323 = t322;
        if (((((uint8_t *)&t323))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t323) + 8, gm_srco4ljquuk5d87, 147, 30);
        gm_b16_8 t324 = GM_LD(gm_b16_8, (((uint8_t *)&t323) + 8));
        gm_b16_8 t325 = t324;
        gm_b16_8 t326 = GM_LD(gm_b16_8, ((uint8_t *)&t325));
        t9 = t326;
        goto me10;
mn315:;
ma30:;
        uint8_t t328 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t328 != ((uint8_t)18ULL)) goto mn327;
        void * t329 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l32 = t329;
        {
            void * t331 = (void *)((uint8_t *)&F.l5);
            void * t332 = gm_l32;
            gm_tftb1odc739t5 t333 = gm_f_push_pf02nv6(t331, t332);
            gm_tftb1odc739t5 t334 = t333;
            if (((((uint8_t *)&t334))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t334) + 1, gm_srco4ljquuk5d87, 149, 13);
            gm_b0_1 t335 = GM_LD(gm_b0_1, (((uint8_t *)&t334) + 1));
        }
        gm_b16_8 t336;
        GM_ST(void *, (uint8_t *)&t336, (void *)(gm_sh4niqnmh73co));
        GM_ST(uintptr_t, (uint8_t *)&t336 + 8, (uintptr_t)(5));
        gm_b16_8 t337 = t336;
        t9 = t337;
        goto me10;
mn327:;
ma31:;
        uint8_t t339 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t339 != ((uint8_t)19ULL)) goto mn338;
        void * t340 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l33 = t340;
        uint32_t t341 = GM_LD(uint32_t, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l34 = t341;
        {
            void * t343 = (void *)((uint8_t *)&F.l5);
            void * t344 = gm_l33;
            gm_tftb1odc739t5 t345 = gm_f_push_pf02nv6(t343, t344);
            gm_tftb1odc739t5 t346 = t345;
            if (((((uint8_t *)&t346))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t346) + 1, gm_srco4ljquuk5d87, 153, 13);
            gm_b0_1 t347 = GM_LD(gm_b0_1, (((uint8_t *)&t346) + 1));
        }
        uint32_t t348 = gm_l34;
        gm_t88rcucg1b7uk t349;
        GM_ST(uint32_t, ((uint8_t *)&t349 + 0), t348);
        gm_tabb710d9mq2e t350;
        gm_f_format_208te7b((void *)&t350, (void *)NULL, cx, NULL, t349);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t351 = t350;
        if (((((uint8_t *)&t351))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t351) + 8, gm_srco4ljquuk5d87, 154, 13);
        gm_b16_8 t352 = GM_LD(gm_b16_8, (((uint8_t *)&t351) + 8));
        gm_b16_8 t353 = t352;
        gm_b16_8 t354 = t353;
        gm_b16_8 t355 = GM_LD(gm_b16_8, ((uint8_t *)&t354));
        t9 = t355;
        goto me10;
mn338:;
ma32:;
        uint8_t t357 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t357 != ((uint8_t)20ULL)) goto mn356;
        void * t358 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l35 = t358;
        void * t359 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l36 = t359;
        uint8_t t360 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l37 = t360;
        {
            void * t362 = (void *)((uint8_t *)&F.l5);
            void * t363 = gm_l35;
            gm_tftb1odc739t5 t364 = gm_f_push_pf02nv6(t362, t363);
            gm_tftb1odc739t5 t365 = t364;
            if (((((uint8_t *)&t365))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t365) + 1, gm_srco4ljquuk5d87, 157, 13);
            gm_b0_1 t366 = GM_LD(gm_b0_1, (((uint8_t *)&t365) + 1));
        }
        {
            void * t367 = (void *)((uint8_t *)&F.l5);
            void * t368 = gm_l36;
            gm_tftb1odc739t5 t369 = gm_f_push_pf02nv6(t367, t368);
            gm_tftb1odc739t5 t370 = t369;
            if (((((uint8_t *)&t370))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t370) + 1, gm_srco4ljquuk5d87, 158, 13);
            gm_b0_1 t371 = GM_LD(gm_b0_1, (((uint8_t *)&t370) + 1));
        }
        gm_b16_8 t372;
        uint8_t t374 = gm_l37;
        if (!t374) goto ie373;
        gm_b16_8 t376;
        GM_ST(void *, (uint8_t *)&t376, (void *)(gm_s5dbqnns2epkg));
        GM_ST(uintptr_t, (uint8_t *)&t376 + 8, (uintptr_t)(5));
        gm_b16_8 t377 = t376;
        t372 = t377;
        goto ix373;
ie373:;
        gm_b16_8 t379;
        GM_ST(void *, (uint8_t *)&t379, (void *)(gm_ssgn9mppmu3vl));
        GM_ST(uintptr_t, (uint8_t *)&t379 + 8, (uintptr_t)(15));
        gm_b16_8 t380 = t379;
        t372 = t380;
ix373:;
        gm_b16_8 t381 = t372;
        t9 = t381;
        goto me10;
mn356:;
ma33:;
        uint8_t t383 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t383 != ((uint8_t)21ULL)) goto mn382;
        gm_tao63pk3r18v5 t384 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t8) + 8) + 8));
        gm_l38 = t384;
        {
            void * t386 = (void *)((uint8_t *)&F.l5);
            void * t387 = GM_LD(void *, (((uint8_t *)&gm_l38) + 0));
            gm_tftb1odc739t5 t388 = gm_f_push_pf02nv6(t386, t387);
            gm_tftb1odc739t5 t389 = t388;
            if (((((uint8_t *)&t389))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t389) + 1, gm_srco4ljquuk5d87, 162, 13);
            gm_b0_1 t390 = GM_LD(gm_b0_1, (((uint8_t *)&t389) + 1));
        }
        {
            void * t392 = GM_LD(void *, (((uint8_t *)&gm_l38) + 8));
            void * t393 = t392;
            if (!(GM_LD(void *, ((uint8_t *)&t393)) != 0)) goto ie391;
            void * t394 = GM_LD(void *, ((uint8_t *)&t393));
            gm_l39 = t394;
            {
                void * t396 = (void *)((uint8_t *)&F.l5);
                void * t397 = gm_l39;
                gm_tftb1odc739t5 t398 = gm_f_push_pf02nv6(t396, t397);
                gm_tftb1odc739t5 t399 = t398;
                if (((((uint8_t *)&t399))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t399) + 1, gm_srco4ljquuk5d87, 163, 29);
                gm_b0_1 t400 = GM_LD(gm_b0_1, (((uint8_t *)&t399) + 1));
            }
            goto ix391;
ie391:;
ix391:;
        }
        {
            void * t402 = GM_LD(void *, (((uint8_t *)&gm_l38) + 16));
            void * t403 = t402;
            if (!(GM_LD(void *, ((uint8_t *)&t403)) != 0)) goto ie401;
            void * t404 = GM_LD(void *, ((uint8_t *)&t403));
            gm_l40 = t404;
            {
                void * t406 = (void *)((uint8_t *)&F.l5);
                void * t407 = gm_l40;
                gm_tftb1odc739t5 t408 = gm_f_push_pf02nv6(t406, t407);
                gm_tftb1odc739t5 t409 = t408;
                if (((((uint8_t *)&t409))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t409) + 1, gm_srco4ljquuk5d87, 164, 29);
                gm_b0_1 t410 = GM_LD(gm_b0_1, (((uint8_t *)&t409) + 1));
            }
            goto ix401;
ie401:;
ix401:;
        }
        gm_tfeab279c16rr t411;
        gm_b16_8 t412;
        void * t414 = GM_LD(void *, (((uint8_t *)&gm_l38) + 8));
        void * t415 = t414;
        uint8_t t416;
        if ((GM_LD(void *, ((uint8_t *)&t415)) != 0)) goto mn418;
        t416 = ((uint8_t)0);
        goto me417;
mn418:;
        t416 = ((uint8_t)1);
        goto me417;
mn419:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 165, 42);
me417:;
        if (!t416) goto ie413;
        gm_b16_8 t421;
        GM_ST(void *, (uint8_t *)&t421, (void *)(gm_sq0h7jjc6dtk5));
        GM_ST(uintptr_t, (uint8_t *)&t421 + 8, (uintptr_t)(3));
        gm_b16_8 t422 = t421;
        t412 = t422;
        goto ix413;
ie413:;
        gm_b16_8 t424;
        GM_ST(void *, (uint8_t *)&t424, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t424 + 8, (uintptr_t)(0));
        gm_b16_8 t425 = t424;
        t412 = t425;
ix413:;
        GM_ST(gm_b16_8, (uint8_t *)&t411 + 0, t412);
        gm_b16_8 t426;
        void * t428 = GM_LD(void *, (((uint8_t *)&gm_l38) + 16));
        void * t429 = t428;
        uint8_t t430;
        if ((GM_LD(void *, ((uint8_t *)&t429)) != 0)) goto mn432;
        t430 = ((uint8_t)0);
        goto me431;
mn432:;
        t430 = ((uint8_t)1);
        goto me431;
mn433:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 165, 86);
me431:;
        if (!t430) goto ie427;
        gm_b16_8 t435;
        GM_ST(void *, (uint8_t *)&t435, (void *)(gm_s9v13eck7gubi));
        GM_ST(uintptr_t, (uint8_t *)&t435 + 8, (uintptr_t)(3));
        gm_b16_8 t436 = t435;
        t426 = t436;
        goto ix427;
ie427:;
        gm_b16_8 t438;
        GM_ST(void *, (uint8_t *)&t438, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t438 + 8, (uintptr_t)(0));
        gm_b16_8 t439 = t438;
        t426 = t439;
ix427:;
        GM_ST(gm_b16_8, (uint8_t *)&t411 + 16, t426);
        gm_b16_8 t440;
        uint8_t t442 = GM_LD(uint8_t, (((uint8_t *)&gm_l38) + 24));
        if (!t442) goto ie441;
        gm_b16_8 t444;
        GM_ST(void *, (uint8_t *)&t444, (void *)(gm_sksprenpp79ih));
        GM_ST(uintptr_t, (uint8_t *)&t444 + 8, (uintptr_t)(10));
        gm_b16_8 t445 = t444;
        t440 = t445;
        goto ix441;
ie441:;
        gm_b16_8 t447;
        GM_ST(void *, (uint8_t *)&t447, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t447 + 8, (uintptr_t)(0));
        gm_b16_8 t448 = t447;
        t440 = t448;
ix441:;
        GM_ST(gm_b16_8, (uint8_t *)&t411 + 32, t440);
        gm_tabb710d9mq2e t449;
        gm_f_format_oc8ipo6((void *)&t449, (void *)NULL, cx, NULL, &t411);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t450 = t449;
        if (((((uint8_t *)&t450))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t450) + 8, gm_srco4ljquuk5d87, 165, 13);
        gm_b16_8 t451 = GM_LD(gm_b16_8, (((uint8_t *)&t450) + 8));
        gm_b16_8 t452 = t451;
        gm_b16_8 t453 = t452;
        gm_b16_8 t454 = GM_LD(gm_b16_8, ((uint8_t *)&t453));
        t9 = t454;
        goto me10;
mn382:;
ma34:;
        uint8_t t456 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t456 != ((uint8_t)22ULL)) goto mn455;
        void * t457 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l41 = t457;
        {
            void * t459 = (void *)((uint8_t *)&F.l5);
            void * t460 = gm_l41;
            gm_tftb1odc739t5 t461 = gm_f_push_pf02nv6(t459, t460);
            gm_tftb1odc739t5 t462 = t461;
            if (((((uint8_t *)&t462))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t462) + 1, gm_srco4ljquuk5d87, 168, 13);
            gm_b0_1 t463 = GM_LD(gm_b0_1, (((uint8_t *)&t462) + 1));
        }
        gm_b16_8 t464;
        GM_ST(void *, (uint8_t *)&t464, (void *)(gm_sstrtuk2b4jdg));
        GM_ST(uintptr_t, (uint8_t *)&t464 + 8, (uintptr_t)(9));
        gm_b16_8 t465 = t464;
        t9 = t465;
        goto me10;
mn455:;
ma35:;
        uint8_t t467 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t467 != ((uint8_t)23ULL)) goto mn466;
        void * t468 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l42 = t468;
        {
            void * t470 = (void *)((uint8_t *)&F.l5);
            void * t471 = gm_l42;
            gm_tftb1odc739t5 t472 = gm_f_push_pf02nv6(t470, t471);
            gm_tftb1odc739t5 t473 = t472;
            if (((((uint8_t *)&t473))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t473) + 1, gm_srco4ljquuk5d87, 172, 13);
            gm_b0_1 t474 = GM_LD(gm_b0_1, (((uint8_t *)&t473) + 1));
        }
        gm_b16_8 t475;
        GM_ST(void *, (uint8_t *)&t475, (void *)(gm_sedrsudco1u16));
        GM_ST(uintptr_t, (uint8_t *)&t475 + 8, (uintptr_t)(9));
        gm_b16_8 t476 = t475;
        t9 = t476;
        goto me10;
mn466:;
ma36:;
        uint8_t t478 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t478 != ((uint8_t)24ULL)) goto mn477;
        void * t479 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l43 = t479;
        {
            void * t481 = (void *)((uint8_t *)&F.l5);
            void * t482 = gm_l43;
            gm_tftb1odc739t5 t483 = gm_f_push_pf02nv6(t481, t482);
            gm_tftb1odc739t5 t484 = t483;
            if (((((uint8_t *)&t484))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t484) + 1, gm_srco4ljquuk5d87, 176, 13);
            gm_b0_1 t485 = GM_LD(gm_b0_1, (((uint8_t *)&t484) + 1));
        }
        gm_b16_8 t486;
        GM_ST(void *, (uint8_t *)&t486, (void *)(gm_sukutl224cgkm));
        GM_ST(uintptr_t, (uint8_t *)&t486 + 8, (uintptr_t)(7));
        gm_b16_8 t487 = t486;
        t9 = t487;
        goto me10;
mn477:;
ma37:;
        uint8_t t489 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t489 != ((uint8_t)25ULL)) goto mn488;
        void * t490 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l44 = t490;
        {
            void * t492 = (void *)((uint8_t *)&F.l5);
            void * t493 = gm_l44;
            gm_tftb1odc739t5 t494 = gm_f_push_pf02nv6(t492, t493);
            gm_tftb1odc739t5 t495 = t494;
            if (((((uint8_t *)&t495))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t495) + 1, gm_srco4ljquuk5d87, 180, 13);
            gm_b0_1 t496 = GM_LD(gm_b0_1, (((uint8_t *)&t495) + 1));
        }
        gm_b16_8 t497;
        GM_ST(void *, (uint8_t *)&t497, (void *)(gm_slgrfk23llffe));
        GM_ST(uintptr_t, (uint8_t *)&t497 + 8, (uintptr_t)(12));
        gm_b16_8 t498 = t497;
        t9 = t498;
        goto me10;
mn488:;
ma38:;
        uint8_t t500 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t500 != ((uint8_t)26ULL)) goto mn499;
        uint32_t t501 = GM_LD(uint32_t, ((((uint8_t *)t8) + 8) + 8));
        gm_l45 = t501;
        void * t502 = gm_l3;
        uint32_t t503 = gm_l45;
        gm_b16_8 t504 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t502, t503);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t505;
        GM_ST(gm_b16_8, ((uint8_t *)&t505 + 0), t504);
        gm_tabb710d9mq2e t506;
        gm_f_format_sugh2p3((void *)&t506, (void *)NULL, cx, NULL, t505);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t507 = t506;
        if (((((uint8_t *)&t507))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t507) + 8, gm_srco4ljquuk5d87, 183, 22);
        gm_b16_8 t508 = GM_LD(gm_b16_8, (((uint8_t *)&t507) + 8));
        gm_b16_8 t509 = t508;
        gm_b16_8 t510 = GM_LD(gm_b16_8, ((uint8_t *)&t509));
        t9 = t510;
        goto me10;
mn499:;
ma39:;
        uint8_t t512 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t512 != ((uint8_t)27ULL)) goto mn511;
        uint32_t t513 = GM_LD(uint32_t, ((((uint8_t *)t8) + 8) + 8));
        gm_l46 = t513;
        void * t514 = gm_l3;
        uint32_t t515 = gm_l46;
        gm_b16_8 t516 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t514, t515);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t517;
        GM_ST(gm_b16_8, ((uint8_t *)&t517 + 0), t516);
        gm_tabb710d9mq2e t518;
        gm_f_format_ka8hqpj((void *)&t518, (void *)NULL, cx, NULL, t517);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t519 = t518;
        if (((((uint8_t *)&t519))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t519) + 8, gm_srco4ljquuk5d87, 184, 24);
        gm_b16_8 t520 = GM_LD(gm_b16_8, (((uint8_t *)&t519) + 8));
        gm_b16_8 t521 = t520;
        gm_b16_8 t522 = GM_LD(gm_b16_8, ((uint8_t *)&t521));
        t9 = t522;
        goto me10;
mn511:;
ma40:;
        uint8_t t524 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t524 != ((uint8_t)28ULL)) goto mn523;
        uint32_t t525 = GM_LD(uint32_t, ((((uint8_t *)t8) + 8) + 8));
        gm_l47 = t525;
        {
            uint32_t t527 = gm_l47;
            uint64_t t528 = (uint64_t)t527;
            uint64_t t529 = (uintptr_t)t528;
            void * t530 = gm_l3;
            uint64_t t531 = GM_LD(uintptr_t, ((((uint8_t *)t530) + 504) + 0) + 8);
            if (t529 >= (uintptr_t)(t531)) gm_trap_index(t529, t531, gm_srco4ljquuk5d87, 186, 20);
            gm_t1cs2fjnrqap1 t532 = GM_LD(gm_t1cs2fjnrqap1, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t530) + 504) + 0)) + t529 * 40));
            F.l48 = t532;
        }
        {
            gm_f_list_epvducg((void *)&F.l49, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t534 = GM_LD(gm_b16_8, (((uint8_t *)&F.l48) + 24));
                gm_l50 = t534;
            }
            {
                gm_l51 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t536 = gm_l51;
                    uint64_t t537 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                    uint8_t t538 = (t536 < t537);
                    if (!t538) goto lb535;
                    {
                        uint64_t t541 = gm_l51;
                        uint64_t t542 = (uintptr_t)t541;
                        uint64_t t543 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                        uint32_t t544 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l50)) + t542 * 4));
                        uint32_t t545 = t544;
                        uint32_t t548 = GM_LD(uint32_t, ((uint8_t *)&t545));
                        gm_l52 = t548;
                        {
                            void * t550 = (void *)((uint8_t *)&F.l49);
                            void * t551 = gm_l3;
                            uint32_t t552 = gm_l52;
                            gm_b16_8 t553 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t551, t552);
                            if (gm_unw.kind) {
                                goto gm_ll539;
                            }
                            gm_tftb1odc739t5 t554 = gm_f_push_3ef746b(t550, t553);
                            gm_tftb1odc739t5 t555 = t554;
                            if (((((uint8_t *)&t555))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t555) + 1, gm_srco4ljquuk5d87, 188, 36);
                            gm_b0_1 t556 = GM_LD(gm_b0_1, (((uint8_t *)&t555) + 1));
                        }
                        goto me546;
mn547:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 188, 13);
me546:;
                    }
lc535:;
                    {
                        uint64_t t558 = gm_l51;
                        uint64_t r559; bool o560 = __builtin_add_overflow(t558, ((uint64_t)1ULL), &r559);
                        uint64_t t561 = r559;
                        gm_l51 = t561;
                    }
                }
gm_ll539:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb535; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc535; }
                }
                goto gm_lad;
lb535:;
            }
        }
        gm_t6j6smj7qoctu t562;
        void * t563 = gm_l3;
        void * t564 = (void *)(((uint8_t *)t563) + 8);
        void * t565 = (void *)(((uint8_t *)&F.l48) + 0);
        gm_b16_8 t566 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t564, t565);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t562 + 0, t566);
        gm_b16_8 t567 = GM_LD(gm_b16_8, (((uint8_t *)&F.l49) + 0));
        gm_b16_8 t568 = t567;
        gm_b16_8 t569 = GM_LD(gm_b16_8, ((uint8_t *)&t568));
        gm_b16_8 t570;
        GM_ST(void *, (uint8_t *)&t570, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t570 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t571;
        gm_f_join_sfknljb((void *)&t571, (void *)NULL, cx, NULL, t569, t570);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t572 = t571;
        if (((((uint8_t *)&t572))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t572) + 8, gm_srco4ljquuk5d87, 189, 77);
        gm_b16_8 t573 = GM_LD(gm_b16_8, (((uint8_t *)&t572) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t562 + 16, t573);
        gm_tabb710d9mq2e t574;
        gm_f_format_cuuc21t((void *)&t574, (void *)NULL, cx, NULL, &t562);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t575 = t574;
        if (((((uint8_t *)&t575))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t575) + 8, gm_srco4ljquuk5d87, 189, 13);
        gm_b16_8 t576 = GM_LD(gm_b16_8, (((uint8_t *)&t575) + 8));
        gm_b16_8 t577 = t576;
        gm_b16_8 t578 = t577;
        gm_b16_8 t579 = GM_LD(gm_b16_8, ((uint8_t *)&t578));
        t9 = t579;
        goto me10;
mn523:;
ma41:;
        uint8_t t581 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t581 != ((uint8_t)58ULL)) goto mn580;
        gm_t710lsrq1m8gk t582 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t8) + 8) + 8));
        F.l53 = t582;
        {
            void * t584 = gm_l0;
            uint64_t t585 = gm_l1;
            gm_b16_8 t586 = gm_l4;
            gm_b0_1 t587 = gm_f_write_with_j37nil7((void *)NULL, cx, NULL, t584, t585, &(*(gm_t710lsrq1m8gk *)((uint8_t *)&F.l53)), t586);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b0_1 t588;
        uint8_t t589 = 0;
        F.ret = t588;
        goto gm_fret;
        goto me10;
mn580:;
ma42:;
        uint8_t t591 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t591 != ((uint8_t)29ULL)) goto mn590;
        gm_t9qkh0rsooc8d t592 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l54 = t592;
        void * t593 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l55 = t593;
        {
            void * t595 = (void *)((uint8_t *)&F.l5);
            void * t596 = gm_l55;
            gm_tftb1odc739t5 t597 = gm_f_push_pf02nv6(t595, t596);
            gm_tftb1odc739t5 t598 = t597;
            if (((((uint8_t *)&t598))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t598) + 1, gm_srco4ljquuk5d87, 196, 13);
            gm_b0_1 t599 = GM_LD(gm_b0_1, (((uint8_t *)&t598) + 1));
        }
        gm_b16_8 t600;
        uint8_t t606 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
        switch (t606) { case ((uint8_t)0ULL): goto ma602; case ((uint8_t)1ULL): goto ma603; case ((uint8_t)2ULL): goto ma604; default: goto ma605; }
ma602:;
        uint8_t t608 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
        if (t608 != ((uint8_t)0ULL)) goto mn607;
        gm_b16_8 t609;
        GM_ST(void *, (uint8_t *)&t609, (void *)(gm_se19nf1421e37));
        GM_ST(uintptr_t, (uint8_t *)&t609 + 8, (uintptr_t)(3));
        t600 = t609;
        goto me601;
mn607:;
ma603:;
        uint8_t t611 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
        if (t611 != ((uint8_t)1ULL)) goto mn610;
        gm_b16_8 t612;
        GM_ST(void *, (uint8_t *)&t612, (void *)(gm_shohh3180gu26));
        GM_ST(uintptr_t, (uint8_t *)&t612 + 8, (uintptr_t)(6));
        t600 = t612;
        goto me601;
mn610:;
ma604:;
        uint8_t t614 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
        if (t614 != ((uint8_t)2ULL)) goto mn613;
        gm_b16_8 t615;
        GM_ST(void *, (uint8_t *)&t615, (void *)(gm_soe4gvhg9ei97));
        GM_ST(uintptr_t, (uint8_t *)&t615 + 8, (uintptr_t)(3));
        t600 = t615;
        goto me601;
mn613:;
ma605:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 197, 36);
me601:;
        gm_tnpifv20irnph t616;
        GM_ST(gm_b16_8, ((uint8_t *)&t616 + 0), t600);
        gm_tabb710d9mq2e t617;
        gm_f_format_r8pfaos((void *)&t617, (void *)NULL, cx, NULL, t616);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t618 = t617;
        if (((((uint8_t *)&t618))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t618) + 8, gm_srco4ljquuk5d87, 197, 13);
        gm_b16_8 t619 = GM_LD(gm_b16_8, (((uint8_t *)&t618) + 8));
        gm_b16_8 t620 = t619;
        gm_b16_8 t621 = t620;
        gm_b16_8 t622 = GM_LD(gm_b16_8, ((uint8_t *)&t621));
        t9 = t622;
        goto me10;
mn590:;
ma43:;
        uint8_t t624 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t624 != ((uint8_t)30ULL)) goto mn623;
        gm_ts6oqppjh0ba3 t625 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l56 = t625;
        void * t626 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l57 = t626;
        void * t627 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l58 = t627;
        {
            void * t629 = (void *)((uint8_t *)&F.l5);
            void * t630 = gm_l57;
            gm_tftb1odc739t5 t631 = gm_f_push_pf02nv6(t629, t630);
            gm_tftb1odc739t5 t632 = t631;
            if (((((uint8_t *)&t632))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t632) + 1, gm_srco4ljquuk5d87, 204, 13);
            gm_b0_1 t633 = GM_LD(gm_b0_1, (((uint8_t *)&t632) + 1));
        }
        {
            void * t634 = (void *)((uint8_t *)&F.l5);
            void * t635 = gm_l58;
            gm_tftb1odc739t5 t636 = gm_f_push_pf02nv6(t634, t635);
            gm_tftb1odc739t5 t637 = t636;
            if (((((uint8_t *)&t637))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t637) + 1, gm_srco4ljquuk5d87, 205, 13);
            gm_b0_1 t638 = GM_LD(gm_b0_1, (((uint8_t *)&t637) + 1));
        }
        gm_ts6oqppjh0ba3 t639 = gm_l56;
        gm_b16_8 t640 = gm_f_binop_text_t4cv0b4((void *)NULL, cx, NULL, t639);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t641;
        GM_ST(gm_b16_8, ((uint8_t *)&t641 + 0), t640);
        gm_tabb710d9mq2e t642;
        gm_f_format_85biid9((void *)&t642, (void *)NULL, cx, NULL, t641);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t643 = t642;
        if (((((uint8_t *)&t643))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t643) + 8, gm_srco4ljquuk5d87, 206, 13);
        gm_b16_8 t644 = GM_LD(gm_b16_8, (((uint8_t *)&t643) + 8));
        gm_b16_8 t645 = t644;
        gm_b16_8 t646 = t645;
        gm_b16_8 t647 = GM_LD(gm_b16_8, ((uint8_t *)&t646));
        t9 = t647;
        goto me10;
mn623:;
ma44:;
        uint8_t t649 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t649 != ((uint8_t)31ULL)) goto mn648;
        uint8_t t650 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l59 = t650;
        void * t651 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l60 = t651;
        void * t652 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l61 = t652;
        {
            void * t654 = (void *)((uint8_t *)&F.l5);
            void * t655 = gm_l60;
            gm_tftb1odc739t5 t656 = gm_f_push_pf02nv6(t654, t655);
            gm_tftb1odc739t5 t657 = t656;
            if (((((uint8_t *)&t657))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t657) + 1, gm_srco4ljquuk5d87, 209, 13);
            gm_b0_1 t658 = GM_LD(gm_b0_1, (((uint8_t *)&t657) + 1));
        }
        {
            void * t659 = (void *)((uint8_t *)&F.l5);
            void * t660 = gm_l61;
            gm_tftb1odc739t5 t661 = gm_f_push_pf02nv6(t659, t660);
            gm_tftb1odc739t5 t662 = t661;
            if (((((uint8_t *)&t662))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t662) + 1, gm_srco4ljquuk5d87, 210, 13);
            gm_b0_1 t663 = GM_LD(gm_b0_1, (((uint8_t *)&t662) + 1));
        }
        gm_b16_8 t664;
        uint8_t t666 = gm_l59;
        if (!t666) goto ie665;
        gm_b16_8 t668;
        GM_ST(void *, (uint8_t *)&t668, (void *)(gm_s920dpvvv6cej));
        GM_ST(uintptr_t, (uint8_t *)&t668 + 8, (uintptr_t)(3));
        gm_b16_8 t669 = t668;
        t664 = t669;
        goto ix665;
ie665:;
        gm_b16_8 t671;
        GM_ST(void *, (uint8_t *)&t671, (void *)(gm_spi47lg4tr80j));
        GM_ST(uintptr_t, (uint8_t *)&t671 + 8, (uintptr_t)(2));
        gm_b16_8 t672 = t671;
        t664 = t672;
ix665:;
        gm_b16_8 t673 = t664;
        t9 = t673;
        goto me10;
mn648:;
ma45:;
        uint8_t t675 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t675 != ((uint8_t)32ULL)) goto mn674;
        gm_tmn7n0n9raou5 t676 = GM_LD(gm_tmn7n0n9raou5, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l62 = t676;
        void * t677 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l63 = t677;
        {
            void * t679 = (void *)((uint8_t *)&F.l5);
            void * t680 = gm_l63;
            gm_tftb1odc739t5 t681 = gm_f_push_pf02nv6(t679, t680);
            gm_tftb1odc739t5 t682 = t681;
            if (((((uint8_t *)&t682))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t682) + 1, gm_srco4ljquuk5d87, 214, 13);
            gm_b0_1 t683 = GM_LD(gm_b0_1, (((uint8_t *)&t682) + 1));
        }
        gm_tmn7n0n9raou5 t684 = gm_l62;
        gm_b16_8 t685 = gm_f_coercion_text_g32oppe((void *)NULL, cx, NULL, t684);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t686;
        GM_ST(gm_b16_8, ((uint8_t *)&t686 + 0), t685);
        gm_tabb710d9mq2e t687;
        gm_f_format_c726p1u((void *)&t687, (void *)NULL, cx, NULL, t686);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t688 = t687;
        if (((((uint8_t *)&t688))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t688) + 8, gm_srco4ljquuk5d87, 215, 13);
        gm_b16_8 t689 = GM_LD(gm_b16_8, (((uint8_t *)&t688) + 8));
        gm_b16_8 t690 = t689;
        gm_b16_8 t691 = t690;
        gm_b16_8 t692 = GM_LD(gm_b16_8, ((uint8_t *)&t691));
        t9 = t692;
        goto me10;
mn674:;
ma46:;
        uint8_t t694 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t694 != ((uint8_t)33ULL)) goto mn693;
        gm_tvdg94bk7aa22 t695 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l64 = t695;
        gm_b16_8 t696 = GM_LD(gm_b16_8, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l65 = t696;
        {
            {
                gm_l67 = ((uint64_t)0ULL);
            }
            {
                uint64_t t699 = GM_LD(uintptr_t, ((uint8_t *)&gm_l65) + 8);
                gm_l66 = t699;
            }
            {
                for (;;) {
                    uint64_t t701 = gm_l67;
                    uint64_t t702 = gm_l66;
                    uint8_t t703 = (t701 < t702);
                    if (!t703) goto lb700;
                    {
                        uint64_t t706 = gm_l67;
                        uint64_t t707 = t706;
                        uint64_t t710 = GM_LD(uint64_t, ((uint8_t *)&t707));
                        gm_l68 = t710;
                        {
                            void * t712 = (void *)((uint8_t *)&F.l5);
                            uint64_t t713 = gm_l68;
                            uint64_t t714 = (uintptr_t)t713;
                            uint64_t t715 = GM_LD(uintptr_t, ((uint8_t *)&gm_l65) + 8);
                            if (t714 >= (uintptr_t)(t715)) gm_trap_index(t714, t715, gm_srco4ljquuk5d87, 218, 47);
                            void * t716 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l65)) + t714 * 224);
                            gm_tftb1odc739t5 t717 = gm_f_push_pf02nv6(t712, t716);
                            gm_tftb1odc739t5 t718 = t717;
                            if (((((uint8_t *)&t718))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t718) + 1, gm_srco4ljquuk5d87, 218, 36);
                            gm_b0_1 t719 = GM_LD(gm_b0_1, (((uint8_t *)&t718) + 1));
                        }
                        goto me708;
mn709:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 218, 13);
me708:;
                    }
lc700:;
                    {
                        uint64_t t721 = gm_l67;
                        uint64_t r722; bool o723 = __builtin_add_overflow(t721, ((uint64_t)1ULL), &r722);
                        uint64_t t724 = r722;
                        gm_l67 = t724;
                    }
                }
lb700:;
            }
        }
        gm_tvdg94bk7aa22 t725 = gm_l64;
        gm_b16_8 t726 = gm_f_builtin_text_od38u7h((void *)NULL, cx, NULL, t725);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t727;
        GM_ST(gm_b16_8, ((uint8_t *)&t727 + 0), t726);
        gm_tabb710d9mq2e t728;
        gm_f_format_02b5heb((void *)&t728, (void *)NULL, cx, NULL, t727);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t729 = t728;
        if (((((uint8_t *)&t729))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t729) + 8, gm_srco4ljquuk5d87, 219, 13);
        gm_b16_8 t730 = GM_LD(gm_b16_8, (((uint8_t *)&t729) + 8));
        gm_b16_8 t731 = t730;
        gm_b16_8 t732 = t731;
        gm_b16_8 t733 = GM_LD(gm_b16_8, ((uint8_t *)&t732));
        t9 = t733;
        goto me10;
mn693:;
ma47:;
        uint8_t t735 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t735 != ((uint8_t)34ULL)) goto mn734;
        gm_tsth9qu1dv0g3 t736 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t8) + 8) + 8) + 0));
        F.l69 = t736;
        gm_b16_8 t737 = GM_LD(gm_b16_8, (((((uint8_t *)t8) + 8) + 8) + 40));
        gm_l70 = t737;
        {
            gm_b16_8 t739;
            uint8_t t745 = GM_LD(uint8_t, (((uint8_t *)&F.l69) + 0));
            switch (t745) { case ((uint8_t)0ULL): goto ma741; case ((uint8_t)1ULL): goto ma742; case ((uint8_t)2ULL): goto ma743; default: goto ma744; }
ma741:;
            uint8_t t747 = GM_LD(uint8_t, (((uint8_t *)&F.l69) + 0));
            if (t747 != ((uint8_t)0ULL)) goto mn746;
            uint32_t t748 = GM_LD(uint32_t, (((uint8_t *)&F.l69) + 8));
            gm_l71 = t748;
            void * t749 = gm_l3;
            uint32_t t750 = gm_l71;
            gm_b16_8 t751 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t749, t750);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t752;
            GM_ST(gm_b16_8, ((uint8_t *)&t752 + 0), t751);
            gm_tabb710d9mq2e t753;
            gm_f_format_vmi8kfe((void *)&t753, (void *)NULL, cx, NULL, t752);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t754 = t753;
            if (((((uint8_t *)&t754))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t754) + 8, gm_srco4ljquuk5d87, 223, 27);
            gm_b16_8 t755 = GM_LD(gm_b16_8, (((uint8_t *)&t754) + 8));
            gm_b16_8 t756 = t755;
            gm_b16_8 t757 = GM_LD(gm_b16_8, ((uint8_t *)&t756));
            t739 = t757;
            goto me740;
mn746:;
ma742:;
            uint8_t t759 = GM_LD(uint8_t, (((uint8_t *)&F.l69) + 0));
            if (t759 != ((uint8_t)1ULL)) goto mn758;
            gm_tiilmfd16v3fc t760 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)&F.l69) + 8) + 0));
            gm_l72 = t760;
            uint32_t t761 = GM_LD(uint32_t, ((((uint8_t *)&F.l69) + 8) + 24));
            gm_l73 = t761;
            gm_tommf4j9upomc t762;
            memset((uint8_t *)&t762 + 20, 0, 4);
            void * t763 = gm_l3;
            void * t764 = (void *)(((uint8_t *)t763) + 8);
            void * t765 = (void *)((uint8_t *)&gm_l72);
            gm_b16_8 t766 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t764, t765);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t762 + 0, t766);
            uint32_t t767 = gm_l73;
            GM_ST(uint32_t, (uint8_t *)&t762 + 16, t767);
            gm_tabb710d9mq2e t768;
            gm_f_format_5m6a3eh((void *)&t768, (void *)NULL, cx, NULL, &t762);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t769 = t768;
            if (((((uint8_t *)&t769))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t769) + 8, gm_srco4ljquuk5d87, 224, 38);
            gm_b16_8 t770 = GM_LD(gm_b16_8, (((uint8_t *)&t769) + 8));
            gm_b16_8 t771 = t770;
            gm_b16_8 t772 = GM_LD(gm_b16_8, ((uint8_t *)&t771));
            t739 = t772;
            goto me740;
mn758:;
ma743:;
            uint8_t t774 = GM_LD(uint8_t, (((uint8_t *)&F.l69) + 0));
            if (t774 != ((uint8_t)2ULL)) goto mn773;
            void * t775 = GM_LD(void *, (((uint8_t *)&F.l69) + 8));
            gm_l74 = t775;
            {
                void * t777 = (void *)((uint8_t *)&F.l5);
                void * t778 = gm_l74;
                gm_tftb1odc739t5 t779 = gm_f_push_pf02nv6(t777, t778);
                gm_tftb1odc739t5 t780 = t779;
                if (((((uint8_t *)&t780))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t780) + 1, gm_srco4ljquuk5d87, 226, 21);
                gm_b0_1 t781 = GM_LD(gm_b0_1, (((uint8_t *)&t780) + 1));
            }
            gm_b16_8 t782;
            GM_ST(void *, (uint8_t *)&t782, (void *)(gm_sj696i9uo1a9s));
            GM_ST(uintptr_t, (uint8_t *)&t782 + 8, (uintptr_t)(13));
            gm_b16_8 t783 = t782;
            t739 = t783;
            goto me740;
mn773:;
ma744:;
            gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 222, 25);
me740:;
            gm_l75 = t739;
        }
        {
            {
                gm_l77 = ((uint64_t)0ULL);
            }
            {
                uint64_t t785 = GM_LD(uintptr_t, ((uint8_t *)&gm_l70) + 8);
                gm_l76 = t785;
            }
            {
                for (;;) {
                    uint64_t t787 = gm_l77;
                    uint64_t t788 = gm_l76;
                    uint8_t t789 = (t787 < t788);
                    if (!t789) goto lb786;
                    {
                        uint64_t t792 = gm_l77;
                        uint64_t t793 = t792;
                        uint64_t t796 = GM_LD(uint64_t, ((uint8_t *)&t793));
                        gm_l78 = t796;
                        {
                            void * t798 = (void *)((uint8_t *)&F.l5);
                            uint64_t t799 = gm_l78;
                            uint64_t t800 = (uintptr_t)t799;
                            uint64_t t801 = GM_LD(uintptr_t, ((uint8_t *)&gm_l70) + 8);
                            if (t800 >= (uintptr_t)(t801)) gm_trap_index(t800, t801, gm_srco4ljquuk5d87, 230, 47);
                            void * t802 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l70)) + t800 * 224);
                            gm_tftb1odc739t5 t803 = gm_f_push_pf02nv6(t798, t802);
                            gm_tftb1odc739t5 t804 = t803;
                            if (((((uint8_t *)&t804))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t804) + 1, gm_srco4ljquuk5d87, 230, 36);
                            gm_b0_1 t805 = GM_LD(gm_b0_1, (((uint8_t *)&t804) + 1));
                        }
                        goto me794;
mn795:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 230, 13);
me794:;
                    }
lc786:;
                    {
                        uint64_t t807 = gm_l77;
                        uint64_t r808; bool o809 = __builtin_add_overflow(t807, ((uint64_t)1ULL), &r808);
                        uint64_t t810 = r808;
                        gm_l77 = t810;
                    }
                }
lb786:;
            }
        }
        gm_b16_8 t811 = gm_l75;
        gm_b16_8 t812 = t811;
        t9 = t812;
        goto me10;
mn734:;
ma48:;
        uint8_t t814 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t814 != ((uint8_t)35ULL)) goto mn813;
        gm_b16_8 t815 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 8) + 8));
        gm_l79 = t815;
        {
            {
                gm_l81 = ((uint64_t)0ULL);
            }
            {
                uint64_t t818 = GM_LD(uintptr_t, ((uint8_t *)&gm_l79) + 8);
                gm_l80 = t818;
            }
            {
                for (;;) {
                    uint64_t t820 = gm_l81;
                    uint64_t t821 = gm_l80;
                    uint8_t t822 = (t820 < t821);
                    if (!t822) goto lb819;
                    {
                        uint64_t t825 = gm_l81;
                        uint64_t t826 = t825;
                        uint64_t t829 = GM_LD(uint64_t, ((uint8_t *)&t826));
                        gm_l82 = t829;
                        {
                            void * t831 = (void *)((uint8_t *)&F.l5);
                            uint64_t t832 = gm_l82;
                            uint64_t t833 = (uintptr_t)t832;
                            uint64_t t834 = GM_LD(uintptr_t, ((uint8_t *)&gm_l79) + 8);
                            if (t833 >= (uintptr_t)(t834)) gm_trap_index(t833, t834, gm_srco4ljquuk5d87, 234, 45);
                            void * t835 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l79)) + t833 * 224);
                            gm_tftb1odc739t5 t836 = gm_f_push_pf02nv6(t831, t835);
                            gm_tftb1odc739t5 t837 = t836;
                            if (((((uint8_t *)&t837))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t837) + 1, gm_srco4ljquuk5d87, 234, 34);
                            gm_b0_1 t838 = GM_LD(gm_b0_1, (((uint8_t *)&t837) + 1));
                        }
                        goto me827;
mn828:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 234, 13);
me827:;
                    }
lc819:;
                    {
                        uint64_t t840 = gm_l81;
                        uint64_t r841; bool o842 = __builtin_add_overflow(t840, ((uint64_t)1ULL), &r841);
                        uint64_t t843 = r841;
                        gm_l81 = t843;
                    }
                }
lb819:;
            }
        }
        gm_b16_8 t844;
        GM_ST(void *, (uint8_t *)&t844, (void *)(gm_sluco9qbar70k));
        GM_ST(uintptr_t, (uint8_t *)&t844 + 8, (uintptr_t)(9));
        gm_b16_8 t845 = t844;
        t9 = t845;
        goto me10;
mn813:;
ma49:;
        uint8_t t847 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t847 != ((uint8_t)36ULL)) goto mn846;
        gm_b16_8 t848 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 8) + 8));
        gm_l83 = t848;
        {
            {
                gm_l85 = ((uint64_t)0ULL);
            }
            {
                uint64_t t851 = GM_LD(uintptr_t, ((uint8_t *)&gm_l83) + 8);
                gm_l84 = t851;
            }
            {
                for (;;) {
                    uint64_t t853 = gm_l85;
                    uint64_t t854 = gm_l84;
                    uint8_t t855 = (t853 < t854);
                    if (!t855) goto lb852;
                    {
                        uint64_t t858 = gm_l85;
                        uint64_t t859 = t858;
                        uint64_t t862 = GM_LD(uint64_t, ((uint8_t *)&t859));
                        gm_l86 = t862;
                        {
                            void * t864 = (void *)((uint8_t *)&F.l5);
                            uint64_t t865 = gm_l86;
                            uint64_t t866 = (uintptr_t)t865;
                            uint64_t t867 = GM_LD(uintptr_t, ((uint8_t *)&gm_l83) + 8);
                            if (t866 >= (uintptr_t)(t867)) gm_trap_index(t866, t867, gm_srco4ljquuk5d87, 238, 45);
                            void * t868 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l83)) + t866 * 224);
                            gm_tftb1odc739t5 t869 = gm_f_push_pf02nv6(t864, t868);
                            gm_tftb1odc739t5 t870 = t869;
                            if (((((uint8_t *)&t870))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t870) + 1, gm_srco4ljquuk5d87, 238, 34);
                            gm_b0_1 t871 = GM_LD(gm_b0_1, (((uint8_t *)&t870) + 1));
                        }
                        goto me860;
mn861:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 238, 13);
me860:;
                    }
lc852:;
                    {
                        uint64_t t873 = gm_l85;
                        uint64_t r874; bool o875 = __builtin_add_overflow(t873, ((uint64_t)1ULL), &r874);
                        uint64_t t876 = r874;
                        gm_l85 = t876;
                    }
                }
lb852:;
            }
        }
        gm_b16_8 t877;
        GM_ST(void *, (uint8_t *)&t877, (void *)(gm_st87fk7ojs4l8));
        GM_ST(uintptr_t, (uint8_t *)&t877 + 8, (uintptr_t)(5));
        gm_b16_8 t878 = t877;
        t9 = t878;
        goto me10;
mn846:;
ma50:;
        uint8_t t880 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t880 != ((uint8_t)37ULL)) goto mn879;
        void * t881 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l87 = t881;
        {
            void * t883 = (void *)((uint8_t *)&F.l5);
            void * t884 = gm_l87;
            gm_tftb1odc739t5 t885 = gm_f_push_pf02nv6(t883, t884);
            gm_tftb1odc739t5 t886 = t885;
            if (((((uint8_t *)&t886))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t886) + 1, gm_srco4ljquuk5d87, 242, 13);
            gm_b0_1 t887 = GM_LD(gm_b0_1, (((uint8_t *)&t886) + 1));
        }
        gm_b16_8 t888;
        GM_ST(void *, (uint8_t *)&t888, (void *)(gm_shvpku7ssg8d7));
        GM_ST(uintptr_t, (uint8_t *)&t888 + 8, (uintptr_t)(10));
        gm_b16_8 t889 = t888;
        t9 = t889;
        goto me10;
mn879:;
ma51:;
        uint8_t t891 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t891 != ((uint8_t)38ULL)) goto mn890;
        gm_ttggq7ng4kp3p t892 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t8) + 8) + 8));
        gm_l88 = t892;
        {
            void * t895 = GM_LD(void *, (((uint8_t *)&gm_l88) + 8));
            void * t896 = t895;
            if (!(GM_LD(void *, ((uint8_t *)&t896)) != 0)) goto ie894;
            void * t897 = GM_LD(void *, ((uint8_t *)&t896));
            gm_l89 = t897;
            {
                void * t899 = (void *)((uint8_t *)&F.l5);
                void * t900 = gm_l89;
                gm_tftb1odc739t5 t901 = gm_f_push_pf02nv6(t899, t900);
                gm_tftb1odc739t5 t902 = t901;
                if (((((uint8_t *)&t902))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t902) + 1, gm_srco4ljquuk5d87, 246, 33);
                gm_b0_1 t903 = GM_LD(gm_b0_1, (((uint8_t *)&t902) + 1));
            }
            goto ix894;
ie894:;
ix894:;
        }
        uint32_t t904 = GM_LD(uint32_t, (((uint8_t *)&gm_l88) + 0));
        gm_t88rcucg1b7uk t905;
        GM_ST(uint32_t, ((uint8_t *)&t905 + 0), t904);
        gm_tabb710d9mq2e t906;
        gm_f_format_lefnhqh((void *)&t906, (void *)NULL, cx, NULL, t905);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t907 = t906;
        if (((((uint8_t *)&t907))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t907) + 8, gm_srco4ljquuk5d87, 247, 13);
        gm_b16_8 t908 = GM_LD(gm_b16_8, (((uint8_t *)&t907) + 8));
        gm_b16_8 t909 = t908;
        gm_b16_8 t910 = t909;
        gm_b16_8 t911 = GM_LD(gm_b16_8, ((uint8_t *)&t910));
        t9 = t911;
        goto me10;
mn890:;
ma52:;
        uint8_t t913 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t913 != ((uint8_t)39ULL)) goto mn912;
        void * t914 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l90 = t914;
        {
            void * t916 = (void *)((uint8_t *)&F.l5);
            void * t917 = gm_l90;
            gm_tftb1odc739t5 t918 = gm_f_push_pf02nv6(t916, t917);
            gm_tftb1odc739t5 t919 = t918;
            if (((((uint8_t *)&t919))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t919) + 1, gm_srco4ljquuk5d87, 250, 13);
            gm_b0_1 t920 = GM_LD(gm_b0_1, (((uint8_t *)&t919) + 1));
        }
        gm_b16_8 t921;
        GM_ST(void *, (uint8_t *)&t921, (void *)(gm_sn581p3lbgrjg));
        GM_ST(uintptr_t, (uint8_t *)&t921 + 8, (uintptr_t)(8));
        gm_b16_8 t922 = t921;
        t9 = t922;
        goto me10;
mn912:;
ma53:;
        uint8_t t924 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t924 != ((uint8_t)41ULL)) goto mn923;
        gm_t3dcoc7077t1q t925 = GM_LD(gm_t3dcoc7077t1q, ((((uint8_t *)t8) + 8) + 8));
        gm_l91 = t925;
        {
            void * t927 = (void *)((uint8_t *)&F.l5);
            void * t928 = GM_LD(void *, (((uint8_t *)&gm_l91) + 0));
            gm_tftb1odc739t5 t929 = gm_f_push_pf02nv6(t927, t928);
            gm_tftb1odc739t5 t930 = t929;
            if (((((uint8_t *)&t930))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t930) + 1, gm_srco4ljquuk5d87, 254, 13);
            gm_b0_1 t931 = GM_LD(gm_b0_1, (((uint8_t *)&t930) + 1));
        }
        {
            void * t932 = (void *)((uint8_t *)&F.l5);
            void * t933 = GM_LD(void *, (((uint8_t *)&gm_l91) + 16));
            gm_tftb1odc739t5 t934 = gm_f_push_pf02nv6(t932, t933);
            gm_tftb1odc739t5 t935 = t934;
            if (((((uint8_t *)&t935))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t935) + 1, gm_srco4ljquuk5d87, 255, 13);
            gm_b0_1 t936 = GM_LD(gm_b0_1, (((uint8_t *)&t935) + 1));
        }
        gm_b16_8 t937;
        if (!(((((uint8_t *)&gm_l91) + 8))[4] != 0)) goto ie938;
        uint32_t t939 = GM_LD(uint32_t, (((uint8_t *)&gm_l91) + 8));
        gm_l92 = t939;
        void * t941 = gm_l0;
        uint32_t t942 = gm_l92;
        gm_b16_8 t943 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t941, t942);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t944;
        GM_ST(gm_b16_8, ((uint8_t *)&t944 + 0), t943);
        gm_tabb710d9mq2e t945;
        gm_f_format_i4fsdjb((void *)&t945, (void *)NULL, cx, NULL, t944);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t946 = t945;
        if (((((uint8_t *)&t946))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t946) + 8, gm_srco4ljquuk5d87, 256, 33);
        gm_b16_8 t947 = GM_LD(gm_b16_8, (((uint8_t *)&t946) + 8));
        gm_b16_8 t948 = t947;
        gm_b16_8 t949 = t948;
        gm_b16_8 t950 = GM_LD(gm_b16_8, ((uint8_t *)&t949));
        t937 = t950;
        goto ix938;
ie938:;
        gm_b16_8 t952;
        GM_ST(void *, (uint8_t *)&t952, (void *)(gm_s9es2tos1bhfp));
        GM_ST(uintptr_t, (uint8_t *)&t952 + 8, (uintptr_t)(4));
        gm_b16_8 t953 = t952;
        t937 = t953;
ix938:;
        gm_b16_8 t954 = t937;
        t9 = t954;
        goto me10;
mn923:;
ma54:;
        uint8_t t956 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t956 != ((uint8_t)42ULL)) goto mn955;
        gm_t083kd7b1kkag t957 = GM_LD(gm_t083kd7b1kkag, ((((uint8_t *)t8) + 8) + 8));
        gm_l93 = t957;
        {
            void * t959 = (void *)((uint8_t *)&F.l5);
            void * t960 = GM_LD(void *, (((uint8_t *)&gm_l93) + 0));
            gm_tftb1odc739t5 t961 = gm_f_push_pf02nv6(t959, t960);
            gm_tftb1odc739t5 t962 = t961;
            if (((((uint8_t *)&t962))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t962) + 1, gm_srco4ljquuk5d87, 259, 13);
            gm_b0_1 t963 = GM_LD(gm_b0_1, (((uint8_t *)&t962) + 1));
        }
        gm_b16_8 t964;
        if (!(((((uint8_t *)&gm_l93) + 8))[4] != 0)) goto ie965;
        uint32_t t966 = GM_LD(uint32_t, (((uint8_t *)&gm_l93) + 8));
        gm_l94 = t966;
        uint32_t t968 = GM_LD(uint32_t, (((uint8_t *)&gm_l93) + 16));
        uint32_t t969 = gm_l94;
        gm_tcc1rq4n1avb2 t970;
        GM_ST(uint32_t, ((uint8_t *)&t970 + 0), t968);
        GM_ST(uint32_t, ((uint8_t *)&t970 + 4), t969);
        gm_tabb710d9mq2e t971;
        gm_f_format_0gd6afr((void *)&t971, (void *)NULL, cx, NULL, t970);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t972 = t971;
        if (((((uint8_t *)&t972))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t972) + 8, gm_srco4ljquuk5d87, 260, 30);
        gm_b16_8 t973 = GM_LD(gm_b16_8, (((uint8_t *)&t972) + 8));
        gm_b16_8 t974 = t973;
        gm_b16_8 t975 = t974;
        gm_b16_8 t976 = GM_LD(gm_b16_8, ((uint8_t *)&t975));
        t964 = t976;
        goto ix965;
ie965:;
        uint32_t t978 = GM_LD(uint32_t, (((uint8_t *)&gm_l93) + 16));
        gm_t88rcucg1b7uk t979;
        GM_ST(uint32_t, ((uint8_t *)&t979 + 0), t978);
        gm_tabb710d9mq2e t980;
        gm_f_format_glqqt6c((void *)&t980, (void *)NULL, cx, NULL, t979);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t981 = t980;
        if (((((uint8_t *)&t981))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t981) + 8, gm_srco4ljquuk5d87, 260, 82);
        gm_b16_8 t982 = GM_LD(gm_b16_8, (((uint8_t *)&t981) + 8));
        gm_b16_8 t983 = t982;
        gm_b16_8 t984 = t983;
        gm_b16_8 t985 = GM_LD(gm_b16_8, ((uint8_t *)&t984));
        t964 = t985;
ix965:;
        gm_b16_8 t986 = t964;
        t9 = t986;
        goto me10;
mn955:;
ma55:;
        uint8_t t988 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t988 != ((uint8_t)40ULL)) goto mn987;
        void * t989 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l95 = t989;
        uint8_t t990 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l96 = t990;
        {
            void * t992 = (void *)((uint8_t *)&F.l5);
            void * t993 = gm_l95;
            gm_tftb1odc739t5 t994 = gm_f_push_pf02nv6(t992, t993);
            gm_tftb1odc739t5 t995 = t994;
            if (((((uint8_t *)&t995))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t995) + 1, gm_srco4ljquuk5d87, 263, 13);
            gm_b0_1 t996 = GM_LD(gm_b0_1, (((uint8_t *)&t995) + 1));
        }
        gm_b16_8 t997;
        uint8_t t999 = gm_l96;
        if (!t999) goto ie998;
        gm_b16_8 t1001;
        GM_ST(void *, (uint8_t *)&t1001, (void *)(gm_sromjese8e416));
        GM_ST(uintptr_t, (uint8_t *)&t1001 + 8, (uintptr_t)(13));
        gm_b16_8 t1002 = t1001;
        t997 = t1002;
        goto ix998;
ie998:;
        gm_b16_8 t1004;
        GM_ST(void *, (uint8_t *)&t1004, (void *)(gm_srntlv7ksi1vk));
        GM_ST(uintptr_t, (uint8_t *)&t1004 + 8, (uintptr_t)(15));
        gm_b16_8 t1005 = t1004;
        t997 = t1005;
ix998:;
        gm_b16_8 t1006 = t997;
        t9 = t1006;
        goto me10;
mn987:;
ma56:;
        uint8_t t1008 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1008 != ((uint8_t)43ULL)) goto mn1007;
        void * t1009 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l97 = t1009;
        void * t1010 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l98 = t1010;
        uint8_t t1011 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l99 = t1011;
        {
            void * t1013 = (void *)((uint8_t *)&F.l5);
            void * t1014 = gm_l97;
            gm_tftb1odc739t5 t1015 = gm_f_push_pf02nv6(t1013, t1014);
            gm_tftb1odc739t5 t1016 = t1015;
            if (((((uint8_t *)&t1016))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1016) + 1, gm_srco4ljquuk5d87, 267, 13);
            gm_b0_1 t1017 = GM_LD(gm_b0_1, (((uint8_t *)&t1016) + 1));
        }
        {
            void * t1018 = (void *)((uint8_t *)&F.l5);
            void * t1019 = gm_l98;
            gm_tftb1odc739t5 t1020 = gm_f_push_pf02nv6(t1018, t1019);
            gm_tftb1odc739t5 t1021 = t1020;
            if (((((uint8_t *)&t1021))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1021) + 1, gm_srco4ljquuk5d87, 268, 13);
            gm_b0_1 t1022 = GM_LD(gm_b0_1, (((uint8_t *)&t1021) + 1));
        }
        gm_b16_8 t1023;
        uint8_t t1025 = gm_l99;
        if (!t1025) goto ie1024;
        gm_b16_8 t1027;
        GM_ST(void *, (uint8_t *)&t1027, (void *)(gm_s3s50a53ci7rk));
        GM_ST(uintptr_t, (uint8_t *)&t1027 + 8, (uintptr_t)(6));
        gm_b16_8 t1028 = t1027;
        t1023 = t1028;
        goto ix1024;
ie1024:;
        gm_b16_8 t1030;
        GM_ST(void *, (uint8_t *)&t1030, (void *)(gm_sgpdmvc9s0d2p));
        GM_ST(uintptr_t, (uint8_t *)&t1030 + 8, (uintptr_t)(2));
        gm_b16_8 t1031 = t1030;
        t1023 = t1031;
ix1024:;
        gm_b16_8 t1032 = t1023;
        t9 = t1032;
        goto me10;
mn1007:;
ma57:;
        uint8_t t1034 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1034 != ((uint8_t)45ULL)) goto mn1033;
        void * t1035 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l100 = t1035;
        void * t1036 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l101 = t1036;
        uint8_t t1037 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l102 = t1037;
        {
            void * t1039 = (void *)((uint8_t *)&F.l5);
            void * t1040 = gm_l100;
            gm_tftb1odc739t5 t1041 = gm_f_push_pf02nv6(t1039, t1040);
            gm_tftb1odc739t5 t1042 = t1041;
            if (((((uint8_t *)&t1042))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1042) + 1, gm_srco4ljquuk5d87, 272, 13);
            gm_b0_1 t1043 = GM_LD(gm_b0_1, (((uint8_t *)&t1042) + 1));
        }
        {
            void * t1044 = (void *)((uint8_t *)&F.l5);
            void * t1045 = gm_l101;
            gm_tftb1odc739t5 t1046 = gm_f_push_pf02nv6(t1044, t1045);
            gm_tftb1odc739t5 t1047 = t1046;
            if (((((uint8_t *)&t1047))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1047) + 1, gm_srco4ljquuk5d87, 273, 13);
            gm_b0_1 t1048 = GM_LD(gm_b0_1, (((uint8_t *)&t1047) + 1));
        }
        gm_b16_8 t1049;
        uint8_t t1051 = gm_l102;
        if (!t1051) goto ie1050;
        gm_b16_8 t1053;
        GM_ST(void *, (uint8_t *)&t1053, (void *)(gm_sh4e4kqha06b3));
        GM_ST(uintptr_t, (uint8_t *)&t1053 + 8, (uintptr_t)(6));
        gm_b16_8 t1054 = t1053;
        t1049 = t1054;
        goto ix1050;
ie1050:;
        gm_b16_8 t1056;
        GM_ST(void *, (uint8_t *)&t1056, (void *)(gm_svbhcvlahd4n2));
        GM_ST(uintptr_t, (uint8_t *)&t1056 + 8, (uintptr_t)(2));
        gm_b16_8 t1057 = t1056;
        t1049 = t1057;
ix1050:;
        gm_b16_8 t1058 = t1049;
        t9 = t1058;
        goto me10;
mn1033:;
ma58:;
        uint8_t t1060 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1060 != ((uint8_t)44ULL)) goto mn1059;
        void * t1061 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l103 = t1061;
        gm_b16_8 t1062 = GM_LD(gm_b16_8, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l104 = t1062;
        {
            void * t1064 = gm_l0;
            uint64_t t1065 = gm_l1;
            gm_b16_8 t1066 = gm_l4;
            gm_tnpifv20irnph t1067;
            GM_ST(gm_b16_8, ((uint8_t *)&t1067 + 0), t1066);
            gm_tabb710d9mq2e t1068;
            gm_f_format_vprps7i((void *)&t1068, (void *)NULL, cx, NULL, t1067);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t1069 = t1068;
            if (((((uint8_t *)&t1069))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1069) + 8, gm_srco4ljquuk5d87, 277, 33);
            gm_b16_8 t1070 = GM_LD(gm_b16_8, (((uint8_t *)&t1069) + 8));
            gm_b16_8 t1071 = t1070;
            gm_b16_8 t1072 = GM_LD(gm_b16_8, ((uint8_t *)&t1071));
            gm_b0_1 t1073 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t1064, t1065, t1072);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t1074 = gm_l0;
            uint64_t t1075 = gm_l1;
            uint64_t r1076; bool o1077 = __builtin_add_overflow(t1075, ((uint64_t)1ULL), &r1076);
            if (o1077) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 278, 26);
            uint64_t t1078 = r1076;
            void * t1079 = gm_l103;
            gm_b0_1 t1080 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t1074, t1078, t1079);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t1082 = gm_l104;
                gm_l105 = t1082;
            }
            {
                gm_l106 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t1084 = gm_l106;
                    uint64_t t1085 = GM_LD(uintptr_t, ((uint8_t *)&gm_l105) + 8);
                    uint8_t t1086 = (t1084 < t1085);
                    if (!t1086) goto lb1083;
                    {
                        uint64_t t1089 = gm_l106;
                        uint64_t t1090 = (uintptr_t)t1089;
                        uint64_t t1091 = GM_LD(uintptr_t, ((uint8_t *)&gm_l105) + 8);
                        gm_tbbqefuhv2r8h t1094 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l105)) + t1090 * 24));
                        gm_l107 = t1094;
                        {
                            void * t1096 = gm_l0;
                            uint64_t t1097 = gm_l1;
                            uint64_t r1098; bool o1099 = __builtin_add_overflow(t1097, ((uint64_t)1ULL), &r1098);
                            if (o1099) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 279, 41);
                            uint64_t t1100 = r1098;
                            void * t1101 = (void *)((uint8_t *)&gm_l107);
                            gm_b0_1 t1102 = gm_f_write_arm_4vimkqo((void *)NULL, cx, NULL, t1096, t1100, t1101);
                            if (gm_unw.kind) {
                                goto gm_ll1087;
                            }
                        }
                        goto me1092;
mn1093:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 279, 13);
me1092:;
                    }
lc1083:;
                    {
                        uint64_t t1104 = gm_l106;
                        uint64_t r1105; bool o1106 = __builtin_add_overflow(t1104, ((uint64_t)1ULL), &r1105);
                        uint64_t t1107 = r1105;
                        gm_l106 = t1107;
                    }
                }
gm_ll1087:;
                if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb1083; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc1083; }
                }
                goto gm_lad;
lb1083:;
            }
        }
        gm_b0_1 t1108;
        uint8_t t1109 = 0;
        F.ret = t1108;
        goto gm_fret;
        goto me10;
mn1059:;
ma59:;
        uint8_t t1111 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1111 != ((uint8_t)46ULL)) goto mn1110;
        void * t1112 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l108 = t1112;
        {
            void * t1114 = gm_l0;
            uint64_t t1115 = gm_l1;
            gm_b16_8 t1116 = gm_l4;
            gm_tnpifv20irnph t1117;
            GM_ST(gm_b16_8, ((uint8_t *)&t1117 + 0), t1116);
            gm_tabb710d9mq2e t1118;
            gm_f_format_rvu96av((void *)&t1118, (void *)NULL, cx, NULL, t1117);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t1119 = t1118;
            if (((((uint8_t *)&t1119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1119) + 8, gm_srco4ljquuk5d87, 283, 33);
            gm_b16_8 t1120 = GM_LD(gm_b16_8, (((uint8_t *)&t1119) + 8));
            gm_b16_8 t1121 = t1120;
            gm_b16_8 t1122 = GM_LD(gm_b16_8, ((uint8_t *)&t1121));
            gm_b0_1 t1123 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t1114, t1115, t1122);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t1124 = gm_l0;
            uint64_t t1125 = gm_l1;
            uint64_t r1126; bool o1127 = __builtin_add_overflow(t1125, ((uint64_t)1ULL), &r1126);
            if (o1127) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 284, 27);
            uint64_t t1128 = r1126;
            void * t1129 = gm_l108;
            gm_b0_1 t1130 = gm_f_write_block_q5mpqjf((void *)NULL, cx, NULL, t1124, t1128, t1129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b0_1 t1131;
        uint8_t t1132 = 0;
        F.ret = t1131;
        goto gm_fret;
        goto me10;
mn1110:;
ma60:;
        uint8_t t1134 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1134 != ((uint8_t)47ULL)) goto mn1133;
        gm_trl2khmuphncd t1135 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t8) + 8) + 8));
        gm_l109 = t1135;
        {
            void * t1137 = gm_l0;
            uint64_t t1138 = gm_l1;
            gm_b16_8 t1139 = gm_l4;
            gm_b0_1 t1140 = gm_f_write_if_oqjiok5((void *)NULL, cx, NULL, t1137, t1138, &(*(gm_trl2khmuphncd *)((uint8_t *)&gm_l109)), t1139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b0_1 t1141;
        uint8_t t1142 = 0;
        F.ret = t1141;
        goto gm_fret;
        goto me10;
mn1133:;
ma61:;
        uint8_t t1144 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1144 != ((uint8_t)48ULL)) goto mn1143;
        gm_t710lsrq1m8gk t1145 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t8) + 8) + 8));
        F.l110 = t1145;
        {
            void * t1147 = gm_l0;
            uint64_t t1148 = gm_l1;
            gm_b16_8 t1149 = gm_l4;
            gm_b0_1 t1150 = gm_f_write_loop_u0ja07u((void *)NULL, cx, NULL, t1147, t1148, &(*(gm_t710lsrq1m8gk *)((uint8_t *)&F.l110)), t1149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b0_1 t1151;
        uint8_t t1152 = 0;
        F.ret = t1151;
        goto gm_fret;
        goto me10;
mn1143:;
ma62:;
        uint8_t t1154 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1154 != ((uint8_t)49ULL)) goto mn1153;
        gm_teo9d4mncjfdu t1155 = GM_LD(gm_teo9d4mncjfdu, ((((uint8_t *)t8) + 8) + 8));
        gm_l111 = t1155;
        {
            void * t1158 = GM_LD(void *, (((uint8_t *)&gm_l111) + 16));
            void * t1159 = t1158;
            if (!(GM_LD(void *, ((uint8_t *)&t1159)) != 0)) goto ie1157;
            void * t1160 = GM_LD(void *, ((uint8_t *)&t1159));
            gm_l112 = t1160;
            {
                void * t1162 = (void *)((uint8_t *)&F.l5);
                void * t1163 = gm_l112;
                gm_tftb1odc739t5 t1164 = gm_f_push_pf02nv6(t1162, t1163);
                gm_tftb1odc739t5 t1165 = t1164;
                if (((((uint8_t *)&t1165))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1165) + 1, gm_srco4ljquuk5d87, 296, 31);
                gm_b0_1 t1166 = GM_LD(gm_b0_1, (((uint8_t *)&t1165) + 1));
            }
            goto ix1157;
ie1157:;
ix1157:;
        }
        gm_te9hrk0u35rev t1167 = GM_LD(gm_te9hrk0u35rev, (((uint8_t *)&gm_l111) + 0));
        gm_b16_8 t1168 = gm_f_target_text_4kebb7t((void *)NULL, cx, NULL, t1167);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t1169;
        GM_ST(gm_b16_8, ((uint8_t *)&t1169 + 0), t1168);
        gm_tabb710d9mq2e t1170;
        gm_f_format_galn15q((void *)&t1170, (void *)NULL, cx, NULL, t1169);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t1171 = t1170;
        if (((((uint8_t *)&t1171))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1171) + 8, gm_srco4ljquuk5d87, 297, 13);
        gm_b16_8 t1172 = GM_LD(gm_b16_8, (((uint8_t *)&t1171) + 8));
        gm_b16_8 t1173 = t1172;
        gm_b16_8 t1174 = t1173;
        gm_b16_8 t1175 = GM_LD(gm_b16_8, ((uint8_t *)&t1174));
        t9 = t1175;
        goto me10;
mn1153:;
ma63:;
        uint8_t t1177 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1177 != ((uint8_t)50ULL)) goto mn1176;
        gm_te9hrk0u35rev t1178 = GM_LD(gm_te9hrk0u35rev, ((((uint8_t *)t8) + 8) + 8));
        gm_l113 = t1178;
        gm_te9hrk0u35rev t1179 = gm_l113;
        gm_b16_8 t1180 = gm_f_target_text_4kebb7t((void *)NULL, cx, NULL, t1179);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t1181;
        GM_ST(gm_b16_8, ((uint8_t *)&t1181 + 0), t1180);
        gm_tabb710d9mq2e t1182;
        gm_f_format_pcmnhpk((void *)&t1182, (void *)NULL, cx, NULL, t1181);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t1183 = t1182;
        if (((((uint8_t *)&t1183))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1183) + 8, gm_srco4ljquuk5d87, 299, 25);
        gm_b16_8 t1184 = GM_LD(gm_b16_8, (((uint8_t *)&t1183) + 8));
        gm_b16_8 t1185 = t1184;
        gm_b16_8 t1186 = GM_LD(gm_b16_8, ((uint8_t *)&t1185));
        t9 = t1186;
        goto me10;
mn1176:;
ma64:;
        uint8_t t1188 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1188 != ((uint8_t)51ULL)) goto mn1187;
        uint32_t t1189 = GM_LD(uint32_t, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l114 = t1189;
        void * t1190 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l115 = t1190;
        {
            void * t1192 = (void *)((uint8_t *)&F.l5);
            void * t1193 = gm_l115;
            gm_tftb1odc739t5 t1194 = gm_f_push_pf02nv6(t1192, t1193);
            gm_tftb1odc739t5 t1195 = t1194;
            if (((((uint8_t *)&t1195))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1195) + 1, gm_srco4ljquuk5d87, 301, 13);
            gm_b0_1 t1196 = GM_LD(gm_b0_1, (((uint8_t *)&t1195) + 1));
        }
        uint32_t t1197 = gm_l114;
        gm_t88rcucg1b7uk t1198;
        GM_ST(uint32_t, ((uint8_t *)&t1198 + 0), t1197);
        gm_tabb710d9mq2e t1199;
        gm_f_format_6na8imi((void *)&t1199, (void *)NULL, cx, NULL, t1198);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t1200 = t1199;
        if (((((uint8_t *)&t1200))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1200) + 8, gm_srco4ljquuk5d87, 302, 13);
        gm_b16_8 t1201 = GM_LD(gm_b16_8, (((uint8_t *)&t1200) + 8));
        gm_b16_8 t1202 = t1201;
        gm_b16_8 t1203 = t1202;
        gm_b16_8 t1204 = GM_LD(gm_b16_8, ((uint8_t *)&t1203));
        t9 = t1204;
        goto me10;
mn1187:;
ma65:;
        uint8_t t1206 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1206 != ((uint8_t)52ULL)) goto mn1205;
        void * t1207 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l116 = t1207;
        {
            void * t1209 = (void *)((uint8_t *)&F.l5);
            void * t1210 = gm_l116;
            gm_tftb1odc739t5 t1211 = gm_f_push_pf02nv6(t1209, t1210);
            gm_tftb1odc739t5 t1212 = t1211;
            if (((((uint8_t *)&t1212))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1212) + 1, gm_srco4ljquuk5d87, 305, 13);
            gm_b0_1 t1213 = GM_LD(gm_b0_1, (((uint8_t *)&t1212) + 1));
        }
        gm_b16_8 t1214;
        GM_ST(void *, (uint8_t *)&t1214, (void *)(gm_sinmpfqtbo9in));
        GM_ST(uintptr_t, (uint8_t *)&t1214 + 8, (uintptr_t)(4));
        gm_b16_8 t1215 = t1214;
        t9 = t1215;
        goto me10;
mn1205:;
ma66:;
        uint8_t t1217 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1217 != ((uint8_t)53ULL)) goto mn1216;
        void * t1218 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l117 = t1218;
        void * t1219 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l118 = t1219;
        {
            void * t1221 = (void *)((uint8_t *)&F.l5);
            void * t1222 = gm_l117;
            gm_tftb1odc739t5 t1223 = gm_f_push_pf02nv6(t1221, t1222);
            gm_tftb1odc739t5 t1224 = t1223;
            if (((((uint8_t *)&t1224))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1224) + 1, gm_srco4ljquuk5d87, 309, 13);
            gm_b0_1 t1225 = GM_LD(gm_b0_1, (((uint8_t *)&t1224) + 1));
        }
        {
            void * t1226 = (void *)((uint8_t *)&F.l5);
            void * t1227 = gm_l118;
            gm_tftb1odc739t5 t1228 = gm_f_push_pf02nv6(t1226, t1227);
            gm_tftb1odc739t5 t1229 = t1228;
            if (((((uint8_t *)&t1229))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1229) + 1, gm_srco4ljquuk5d87, 310, 13);
            gm_b0_1 t1230 = GM_LD(gm_b0_1, (((uint8_t *)&t1229) + 1));
        }
        gm_b16_8 t1231;
        GM_ST(void *, (uint8_t *)&t1231, (void *)(gm_sn6sqlran7h3r));
        GM_ST(uintptr_t, (uint8_t *)&t1231 + 8, (uintptr_t)(6));
        gm_b16_8 t1232 = t1231;
        t9 = t1232;
        goto me10;
mn1216:;
ma67:;
        uint8_t t1234 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1234 != ((uint8_t)54ULL)) goto mn1233;
        void * t1235 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l119 = t1235;
        {
            void * t1237 = (void *)((uint8_t *)&F.l5);
            void * t1238 = gm_l119;
            gm_tftb1odc739t5 t1239 = gm_f_push_pf02nv6(t1237, t1238);
            gm_tftb1odc739t5 t1240 = t1239;
            if (((((uint8_t *)&t1240))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1240) + 1, gm_srco4ljquuk5d87, 314, 13);
            gm_b0_1 t1241 = GM_LD(gm_b0_1, (((uint8_t *)&t1240) + 1));
        }
        gm_b16_8 t1242;
        GM_ST(void *, (uint8_t *)&t1242, (void *)(gm_si1m7q6ia0hto));
        GM_ST(uintptr_t, (uint8_t *)&t1242 + 8, (uintptr_t)(6));
        gm_b16_8 t1243 = t1242;
        t9 = t1243;
        goto me10;
mn1233:;
ma68:;
        uint8_t t1245 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1245 != ((uint8_t)55ULL)) goto mn1244;
        gm_tntn2tf15puib t1246 = GM_LD(gm_tntn2tf15puib, ((((uint8_t *)t8) + 8) + 8));
        F.l120 = t1246;
        {
            uint8_t t1249 = GM_LD(uint8_t, ((((uint8_t *)&F.l120) + 0) + 0));
            if (t1249 != ((uint8_t)1ULL)) goto ie1248;
            void * t1250 = GM_LD(void *, ((((uint8_t *)&F.l120) + 0) + 8));
            gm_l121 = t1250;
            {
                void * t1252 = (void *)((uint8_t *)&F.l5);
                void * t1253 = gm_l121;
                gm_tftb1odc739t5 t1254 = gm_f_push_pf02nv6(t1252, t1253);
                gm_tftb1odc739t5 t1255 = t1254;
                if (((((uint8_t *)&t1255))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1255) + 1, gm_srco4ljquuk5d87, 318, 35);
                gm_b0_1 t1256 = GM_LD(gm_b0_1, (((uint8_t *)&t1255) + 1));
            }
            goto ix1248;
ie1248:;
ix1248:;
        }
        {
            {
                gm_l123 = ((uint64_t)0ULL);
            }
            {
                uint64_t t1258 = GM_LD(uintptr_t, (((uint8_t *)&F.l120) + 72) + 8);
                gm_l122 = t1258;
            }
            {
                for (;;) {
                    uint64_t t1260 = gm_l123;
                    uint64_t t1261 = gm_l122;
                    uint8_t t1262 = (t1260 < t1261);
                    if (!t1262) goto lb1259;
                    {
                        uint64_t t1265 = gm_l123;
                        uint64_t t1266 = t1265;
                        uint64_t t1269 = GM_LD(uint64_t, ((uint8_t *)&t1266));
                        gm_l124 = t1269;
                        {
                            void * t1271 = (void *)((uint8_t *)&F.l5);
                            uint64_t t1272 = gm_l124;
                            uint64_t t1273 = (uintptr_t)t1272;
                            uint64_t t1274 = GM_LD(uintptr_t, (((uint8_t *)&F.l120) + 72) + 8);
                            if (t1273 >= (uintptr_t)(t1274)) gm_trap_index(t1273, t1274, gm_srco4ljquuk5d87, 319, 49);
                            void * t1275 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l120) + 72)) + t1273 * 224);
                            gm_tftb1odc739t5 t1276 = gm_f_push_pf02nv6(t1271, t1275);
                            gm_tftb1odc739t5 t1277 = t1276;
                            if (((((uint8_t *)&t1277))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1277) + 1, gm_srco4ljquuk5d87, 319, 38);
                            gm_b0_1 t1278 = GM_LD(gm_b0_1, (((uint8_t *)&t1277) + 1));
                        }
                        goto me1267;
mn1268:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 319, 13);
me1267:;
                    }
lc1259:;
                    {
                        uint64_t t1280 = gm_l123;
                        uint64_t r1281; bool o1282 = __builtin_add_overflow(t1280, ((uint64_t)1ULL), &r1281);
                        uint64_t t1283 = r1281;
                        gm_l123 = t1283;
                    }
                }
lb1259:;
            }
        }
        void * t1284 = gm_l0;
        gm_b16_8 t1285 = gm_f_write_text_h6e1ibp((void *)NULL, cx, NULL, t1284, &(*(gm_tntn2tf15puib *)((uint8_t *)&F.l120)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t1286 = t1285;
        t9 = t1286;
        goto me10;
mn1244:;
ma69:;
        uint8_t t1288 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1288 != ((uint8_t)56ULL)) goto mn1287;
        gm_t9i38ih07bojq t1289 = GM_LD(gm_t9i38ih07bojq, ((((uint8_t *)t8) + 8) + 8));
        F.l125 = t1289;
        {
            uint8_t t1292 = GM_LD(uint8_t, ((((uint8_t *)&F.l125) + 0) + 0));
            if (t1292 != ((uint8_t)1ULL)) goto ie1291;
            void * t1293 = GM_LD(void *, ((((uint8_t *)&F.l125) + 0) + 8));
            gm_l126 = t1293;
            {
                void * t1295 = (void *)((uint8_t *)&F.l5);
                void * t1296 = gm_l126;
                gm_tftb1odc739t5 t1297 = gm_f_push_pf02nv6(t1295, t1296);
                gm_tftb1odc739t5 t1298 = t1297;
                if (((((uint8_t *)&t1298))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1298) + 1, gm_srco4ljquuk5d87, 323, 35);
                gm_b0_1 t1299 = GM_LD(gm_b0_1, (((uint8_t *)&t1298) + 1));
            }
            goto ix1291;
ie1291:;
ix1291:;
        }
        {
            void * t1300 = (void *)((uint8_t *)&F.l5);
            void * t1301 = GM_LD(void *, (((uint8_t *)&F.l125) + 40));
            gm_tftb1odc739t5 t1302 = gm_f_push_pf02nv6(t1300, t1301);
            gm_tftb1odc739t5 t1303 = t1302;
            if (((((uint8_t *)&t1303))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1303) + 1, gm_srco4ljquuk5d87, 324, 13);
            gm_b0_1 t1304 = GM_LD(gm_b0_1, (((uint8_t *)&t1303) + 1));
        }
        {
            void * t1306 = GM_LD(void *, (((uint8_t *)&F.l125) + 48));
            void * t1307 = t1306;
            if (!(GM_LD(void *, ((uint8_t *)&t1307)) != 0)) goto ie1305;
            void * t1308 = GM_LD(void *, ((uint8_t *)&t1307));
            gm_l127 = t1308;
            {
                void * t1310 = (void *)((uint8_t *)&F.l5);
                void * t1311 = gm_l127;
                gm_tftb1odc739t5 t1312 = gm_f_push_pf02nv6(t1310, t1311);
                gm_tftb1odc739t5 t1313 = t1312;
                if (((((uint8_t *)&t1313))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1313) + 1, gm_srco4ljquuk5d87, 325, 30);
                gm_b0_1 t1314 = GM_LD(gm_b0_1, (((uint8_t *)&t1313) + 1));
            }
            goto ix1305;
ie1305:;
ix1305:;
        }
        gm_t6j6smj7qoctu t1315;
        void * t1316 = gm_l0;
        gm_b16_8 t1317 = gm_f_sink_text_s4o8tr0((void *)NULL, cx, NULL, t1316, &(*(gm_th1g69d7vpipg *)(((uint8_t *)&F.l125) + 0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t1315 + 0, t1317);
        void * t1318 = gm_l0;
        uint32_t t1319 = GM_LD(uint32_t, (((uint8_t *)&F.l125) + 56));
        gm_b16_8 t1320 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t1318, t1319);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t1315 + 16, t1320);
        gm_tabb710d9mq2e t1321;
        gm_f_format_llf38gp((void *)&t1321, (void *)NULL, cx, NULL, &t1315);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t1322 = t1321;
        if (((((uint8_t *)&t1322))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1322) + 8, gm_srco4ljquuk5d87, 326, 13);
        gm_b16_8 t1323 = GM_LD(gm_b16_8, (((uint8_t *)&t1322) + 8));
        gm_b16_8 t1324 = t1323;
        gm_b16_8 t1325 = t1324;
        gm_b16_8 t1326 = GM_LD(gm_b16_8, ((uint8_t *)&t1325));
        t9 = t1326;
        goto me10;
mn1287:;
ma70:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 125, 20);
me10:;
        gm_l128 = t9;
    }
    {
        void * t1327 = gm_l0;
        uint64_t t1328 = gm_l1;
        gm_t6j6smj7qoctu t1329;
        gm_b16_8 t1330 = gm_l128;
        GM_ST(gm_b16_8, (uint8_t *)&t1329 + 0, t1330);
        gm_b16_8 t1331 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t1329 + 16, t1331);
        gm_tabb710d9mq2e t1332;
        gm_f_format_r0u4lpe((void *)&t1332, (void *)NULL, cx, NULL, &t1329);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t1333 = t1332;
        if (((((uint8_t *)&t1333))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1333) + 8, gm_srco4ljquuk5d87, 329, 25);
        gm_b16_8 t1334 = GM_LD(gm_b16_8, (((uint8_t *)&t1333) + 8));
        gm_b16_8 t1335 = t1334;
        gm_b16_8 t1336 = GM_LD(gm_b16_8, ((uint8_t *)&t1335));
        gm_b0_1 t1337 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t1327, t1328, t1336);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t1339 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_l129 = t1339;
        }
        {
            gm_l130 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t1341 = gm_l130;
                uint64_t t1342 = GM_LD(uintptr_t, ((uint8_t *)&gm_l129) + 8);
                uint8_t t1343 = (t1341 < t1342);
                if (!t1343) goto lb1340;
                {
                    uint64_t t1346 = gm_l130;
                    uint64_t t1347 = (uintptr_t)t1346;
                    uint64_t t1348 = GM_LD(uintptr_t, ((uint8_t *)&gm_l129) + 8);
                    void * t1349 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l129)) + t1347 * 8));
                    void * t1350 = t1349;
                    void * t1353 = GM_LD(void *, ((uint8_t *)&t1350));
                    gm_l131 = t1353;
                    {
                        void * t1355 = gm_l0;
                        uint64_t t1356 = gm_l1;
                        uint64_t r1357; bool o1358 = __builtin_add_overflow(t1356, ((uint64_t)1ULL), &r1357);
                        if (o1358) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 330, 40);
                        uint64_t t1359 = r1357;
                        void * t1360 = gm_l131;
                        gm_b0_1 t1361 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t1355, t1359, t1360);
                        if (gm_unw.kind) {
                            goto gm_ll1344;
                        }
                    }
                    goto me1351;
mn1352:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 330, 5);
me1351:;
                }
lc1340:;
                {
                    uint64_t t1363 = gm_l130;
                    uint64_t r1364; bool o1365 = __builtin_add_overflow(t1363, ((uint64_t)1ULL), &r1364);
                    uint64_t t1366 = r1364;
                    gm_l130 = t1366;
                }
            }
gm_ll1344:;
            if (gm_unw.frame == &F.h && gm_unw.label == 8) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb1340; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc1340; }
            }
            goto gm_lad;
lb1340:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_fn_jkakt7l(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_write_fn_jkakt7l F;
    void * gm_l0; /* d */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* s */
    void * gm_l3; /* sig */
    void * gm_l4; /* def */
    gm_b16_8 gm_l5; /* kind */
    gm_b16_8 gm_l6; /* head */
    uint32_t gm_l7; /* p */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint8_t gm_l12; /* param */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    void * gm_l17; /* b */
    GM_STACK_CHECK(&F, 2100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        GM_ST(uint32_t, (((uint8_t *)t2) + 48), t3);
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        gm_l2 = t5;
    }
    {
        uint32_t t6 = gm_l1;
        uint64_t t7 = (uint64_t)t6;
        uint64_t t8 = (uintptr_t)t7;
        void * t9 = gm_l2;
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 344) + 0) + 8);
        if (t8 >= (uintptr_t)(t10)) gm_trap_index(t8, t10, gm_srco4ljquuk5d87, 77, 13);
        void * t11 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t9) + 344) + 0)) + t8 * 136);
        gm_l3 = t11;
    }
    {
        uint32_t t12 = gm_l1;
        uint64_t t13 = (uint64_t)t12;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l2;
        uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 384) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srco4ljquuk5d87, 78, 13);
        void * t17 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 384) + 0)) + t14 * 64);
        gm_l4 = t17;
    }
    {
        void * t18 = gm_l4;
        gm_b16_8 t19;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t18) + 0) + 0));
        switch (t26) { case ((uint8_t)0ULL): goto ma21; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma23; case ((uint8_t)3ULL): goto ma24; default: goto ma25; }
ma21:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t18) + 0) + 0));
        if (t28 != ((uint8_t)0ULL)) goto mn27;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s19ndlkgdtviu));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(5));
        t19 = t29;
        goto me20;
mn27:;
ma22:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t18) + 0) + 0));
        if (t31 != ((uint8_t)1ULL)) goto mn30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s4lo69slu4igt));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(7));
        t19 = t32;
        goto me20;
mn30:;
ma23:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t18) + 0) + 0));
        if (t34 != ((uint8_t)2ULL)) goto mn33;
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s7dkbj8tvt9e3));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(6));
        t19 = t35;
        goto me20;
mn33:;
ma24:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t18) + 0) + 0));
        if (t37 != ((uint8_t)3ULL)) goto mn36;
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sv0fbf330lmcd));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(7));
        t19 = t38;
        goto me20;
mn36:;
ma25:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 79, 13);
me20:;
        gm_l5 = t19;
    }
    {
        gm_t6j6smj7qoctu t39;
        void * t40 = gm_l2;
        uint32_t t41 = gm_l1;
        gm_b16_8 t42 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t39 + 0, t42);
        gm_b16_8 t43 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t39 + 16, t43);
        gm_tabb710d9mq2e t44;
        gm_f_format_9nv39ua((void *)&t44, (void *)NULL, cx, NULL, &t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t45 = t44;
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_srco4ljquuk5d87, 85, 13);
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
        gm_l6 = t46;
    }
    {
        void * t48 = gm_l4;
        if (!(((((uint8_t *)t48) + 48))[4] != 0)) goto ie47;
        uint32_t t49 = GM_LD(uint32_t, (((uint8_t *)t48) + 48));
        gm_l7 = t49;
        {
            gm_t6j6smj7qoctu t51;
            gm_b16_8 t52 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t51 + 0, t52);
            void * t53 = gm_l2;
            uint32_t t54 = gm_l7;
            gm_b16_8 t55 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t51 + 16, t55);
            gm_tabb710d9mq2e t56;
            gm_f_format_p2bh4bb((void *)&t56, (void *)NULL, cx, NULL, &t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t57 = t56;
            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 8, gm_srco4ljquuk5d87, 86, 33);
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t57) + 8));
            gm_l6 = t58;
        }
        goto ix47;
ie47:;
ix47:;
    }
    {
        void * t59 = gm_l0;
        gm_b16_8 t60 = gm_l6;
        gm_b16_8 t61 = t60;
        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
        gm_b0_1 t63 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t59, ((uint64_t)0ULL), t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t65 = gm_l4;
            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)t65) + 24));
            gm_b16_8 t67 = t66;
            gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
            gm_l8 = t68;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t70 = gm_l9;
                uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t72 = (t70 < t71);
                if (!t72) goto lb69;
                {
                    gm_t5im2v50m3hk6 t75;
                    uint64_t t76 = gm_l9;
                    GM_ST(uint64_t, (uint8_t *)&t75 + 0, t76);
                    uint64_t t77 = gm_l9;
                    uint64_t t78 = (uintptr_t)t77;
                    uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    memcpy((uint8_t *)&t75 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t78 * 40), 40);
                    gm_t5im2v50m3hk6 t80 = t75;
                    uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&t80) + 0));
                    gm_l10 = t83;
                    gm_tsoie1ppgdvcg t84 = GM_LD(gm_tsoie1ppgdvcg, (((uint8_t *)&t80) + 8));
                    F.l11 = t84;
                    {
                        void * t86 = gm_l4;
                        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)t86) + 8));
                        gm_b16_8 t88 = t87;
                        gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
                        uint64_t t90 = gm_l10;
                        uint32_t t91 = (uint32_t)t90;
                        gm_tf5srh4mp4b3i t92 = gm_f_index_of_41b084m(t89, t91);
                        gm_tf5srh4mp4b3i t93 = t92;
                        uint8_t t94;
                        if (((((uint8_t *)&t93))[8] != 0)) goto mn96;
                        t94 = ((uint8_t)0);
                        goto me95;
mn96:;
                        t94 = ((uint8_t)1);
                        goto me95;
mn97:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 89, 18);
me95:;
                        gm_l12 = t94;
                    }
                    {
                        void * t98 = gm_l0;
                        gm_tcrdb1inj22os t99;
                        gm_b16_8 t100;
                        uint8_t t102 = gm_l12;
                        if (!t102) goto ie101;
                        gm_b16_8 t104;
                        GM_ST(void *, (uint8_t *)&t104, (void *)(gm_skissj10nclhr));
                        GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(5));
                        gm_b16_8 t105 = t104;
                        t100 = t105;
                        goto ix101;
ie101:;
                        gm_b16_8 t107;
                        GM_ST(void *, (uint8_t *)&t107, (void *)(gm_sjg1ri23hm753));
                        GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(5));
                        gm_b16_8 t108 = t107;
                        t100 = t108;
ix101:;
                        GM_ST(gm_b16_8, (uint8_t *)&t99 + 0, t100);
                        uint64_t t109 = gm_l10;
                        GM_ST(uint64_t, (uint8_t *)&t99 + 16, t109);
                        gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t99 + 24, t110);
                        void * t111 = gm_l2;
                        gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                        gm_b16_8 t113 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t111, t112);
                        if (gm_unw.kind) {
                            goto gm_ll73;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t99 + 40, t113);
                        gm_b16_8 t114;
                        uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 20));
                        if (!t116) goto ie115;
                        gm_b16_8 t118;
                        GM_ST(void *, (uint8_t *)&t118, (void *)(gm_s6ag7edtaqqri));
                        GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(4));
                        gm_b16_8 t119 = t118;
                        t114 = t119;
                        goto ix115;
ie115:;
                        gm_b16_8 t121;
                        GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(0));
                        gm_b16_8 t122 = t121;
                        t114 = t122;
ix115:;
                        GM_ST(gm_b16_8, (uint8_t *)&t99 + 56, t114);
                        gm_tabb710d9mq2e t123;
                        gm_f_format_i7k9v76((void *)&t123, (void *)NULL, cx, NULL, &t99);
                        if (gm_unw.kind) {
                            goto gm_ll73;
                        }
                        gm_tabb710d9mq2e t124 = t123;
                        if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 8, gm_srco4ljquuk5d87, 90, 25);
                        gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 8));
                        gm_b16_8 t126 = t125;
                        gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
                        gm_b0_1 t128 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t98, ((uint64_t)1ULL), t127);
                        if (gm_unw.kind) {
                            goto gm_ll73;
                        }
                    }
                    goto me81;
mn82:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 88, 5);
me81:;
                }
lc69:;
                {
                    uint64_t t130 = gm_l9;
                    uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
                    uint64_t t133 = r131;
                    gm_l9 = t133;
                }
            }
gm_ll73:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb69; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc69; }
            }
            goto gm_lad;
lb69:;
        }
    }
    {
        void * t134 = gm_l0;
        void * t135 = gm_l2;
        void * t136 = gm_l3;
        gm_t88rcucg1b7uk t137 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t136) + 56));
        gm_b16_8 t138 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t135, t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t139;
        GM_ST(gm_b16_8, ((uint8_t *)&t139 + 0), t138);
        gm_tabb710d9mq2e t140;
        gm_f_format_8k3m4s1((void *)&t140, (void *)NULL, cx, NULL, t139);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t141 = t140;
        if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 8, gm_srco4ljquuk5d87, 92, 21);
        gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&t141) + 8));
        gm_b16_8 t143 = t142;
        gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
        gm_b0_1 t145 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t134, ((uint64_t)1ULL), t144);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t147 = gm_l3;
        uint64_t t148 = GM_LD(uintptr_t, ((((uint8_t *)t147) + 64) + 0) + 8);
        uint8_t t149 = (t148 > ((uint64_t)0ULL));
        if (!t149) goto ie146;
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
                void * t152 = gm_l3;
                uint64_t t153 = GM_LD(uintptr_t, ((((uint8_t *)t152) + 64) + 0) + 8);
                gm_l14 = t153;
            }
            {
                for (;;) {
                    uint64_t t155 = gm_l15;
                    uint64_t t156 = gm_l14;
                    uint8_t t157 = (t155 < t156);
                    if (!t157) goto lb154;
                    {
                        uint64_t t160 = gm_l15;
                        uint64_t t161 = t160;
                        uint64_t t164 = GM_LD(uint64_t, ((uint8_t *)&t161));
                        gm_l16 = t164;
                        {
                            void * t166 = (void *)((uint8_t *)&F.l13);
                            void * t167 = gm_l2;
                            void * t168 = (void *)(((uint8_t *)t167) + 8);
                            uint64_t t169 = gm_l16;
                            uint64_t t170 = (uintptr_t)t169;
                            void * t171 = gm_l3;
                            uint64_t t172 = GM_LD(uintptr_t, ((((uint8_t *)t171) + 64) + 0) + 8);
                            if (t170 >= (uintptr_t)(t172)) gm_trap_index(t170, t172, gm_srco4ljquuk5d87, 95, 74);
                            void * t173 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t171) + 64) + 0)) + t170 * 24);
                            gm_b16_8 t174 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t168, t173);
                            if (gm_unw.kind) {
                                goto gm_ll158;
                            }
                            gm_tftb1odc739t5 t175 = gm_f_push_3ef746b(t166, t174);
                            gm_tftb1odc739t5 t176 = t175;
                            if (((((uint8_t *)&t176))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t176) + 1, gm_srco4ljquuk5d87, 95, 47);
                            gm_b0_1 t177 = GM_LD(gm_b0_1, (((uint8_t *)&t176) + 1));
                        }
                        goto me162;
mn163:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 95, 9);
me162:;
                    }
lc154:;
                    {
                        uint64_t t179 = gm_l15;
                        uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)1ULL), &r180);
                        uint64_t t182 = r180;
                        gm_l15 = t182;
                    }
                }
gm_ll158:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb154; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc154; }
                }
                goto gm_lad;
lb154:;
            }
        }
        {
            void * t183 = gm_l0;
            gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
            gm_b16_8 t185 = t184;
            gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
            gm_b16_8 t187;
            GM_ST(void *, (uint8_t *)&t187, (void *)(gm_s0m0qaokk51g0));
            GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(3));
            gm_tabb710d9mq2e t188;
            gm_f_join_sfknljb((void *)&t188, (void *)NULL, cx, NULL, t186, t187);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t189 = t188;
            if (((((uint8_t *)&t189))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t189) + 8, gm_srco4ljquuk5d87, 96, 48);
            gm_b16_8 t190 = GM_LD(gm_b16_8, (((uint8_t *)&t189) + 8));
            gm_tnpifv20irnph t191;
            GM_ST(gm_b16_8, ((uint8_t *)&t191 + 0), t190);
            gm_tabb710d9mq2e t192;
            gm_f_format_7316bju((void *)&t192, (void *)NULL, cx, NULL, t191);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t193 = t192;
            if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 8, gm_srco4ljquuk5d87, 96, 25);
            gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&t193) + 8));
            gm_b16_8 t195 = t194;
            gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&t195));
            gm_b0_1 t197 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t183, ((uint64_t)1ULL), t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix146;
ie146:;
ix146:;
    }
    {
        void * t199 = gm_l4;
        void * t200 = GM_LD(void *, (((uint8_t *)t199) + 40));
        void * t201 = t200;
        if (!(GM_LD(void *, ((uint8_t *)&t201)) != 0)) goto ie198;
        void * t202 = GM_LD(void *, ((uint8_t *)&t201));
        gm_l17 = t202;
        {
            void * t204 = gm_l0;
            void * t205 = gm_l17;
            gm_b0_1 t206 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t204, ((uint64_t)1ULL), t205);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix198;
ie198:;
        {
            void * t208 = gm_l0;
            gm_b16_8 t209;
            GM_ST(void *, (uint8_t *)&t209, (void *)(gm_st7pul9bg6mqu));
            GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(7));
            gm_b0_1 t210 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t208, ((uint64_t)1ULL), t209);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix198:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_if_oqjiok5(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3) {
    struct gm_fr_write_if_oqjiok5 F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_trl2khmuphncd gm_l2; /* x */
    gm_b16_8 gm_l3; /* ty */
    void * gm_l4; /* el */
    GM_STACK_CHECK(&F, 456u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_b16_8 t4 = gm_l3;
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tabb710d9mq2e t6;
        gm_f_format_7ejl94l((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srco4ljquuk5d87, 364, 25);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_b0_1 t11 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t2, t3, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        uint64_t t13 = gm_l1;
        uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)1ULL), &r14);
        if (o15) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 365, 19);
        uint64_t t16 = r14;
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_b16_8 t18 = t17;
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        gm_b0_1 t20 = gm_f_write_conds_k9e57un((void *)NULL, cx, NULL, t12, t16, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t21 = gm_l0;
        uint64_t t22 = gm_l1;
        uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
        if (o24) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 366, 18);
        uint64_t t25 = r23;
        gm_b16_8 t26;
        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sofbtko7t8taa));
        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(4));
        gm_b0_1 t27 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t21, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t28 = gm_l0;
        uint64_t t29 = gm_l1;
        uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)2ULL), &r30);
        if (o31) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 367, 18);
        uint64_t t32 = r30;
        void * t33 = GM_LD(void *, (((uint8_t *)a2) + 16));
        gm_b0_1 t34 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t28, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t36 = GM_LD(void *, (((uint8_t *)a2) + 24));
        void * t37 = t36;
        if (!(GM_LD(void *, ((uint8_t *)&t37)) != 0)) goto ie35;
        void * t38 = GM_LD(void *, ((uint8_t *)&t37));
        gm_l4 = t38;
        {
            void * t40 = gm_l0;
            uint64_t t41 = gm_l1;
            uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
            if (o43) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 369, 22);
            uint64_t t44 = r42;
            gm_b16_8 t45;
            GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s9es2tos1bhfp));
            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(4));
            gm_b0_1 t46 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t40, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t47 = gm_l0;
            uint64_t t48 = gm_l1;
            uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)2ULL), &r49);
            if (o50) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 370, 22);
            uint64_t t51 = r49;
            void * t52 = gm_l4;
            gm_b0_1 t53 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t47, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix35;
ie35:;
ix35:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_line_d6lkps7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_write_line_d6lkps7 F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_b16_8 gm_l2; /* text */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 197u);
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
            uint64_t t3 = gm_l1;
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
                    {
                        void * t15 = gm_l0;
                        void * t16 = (void *)(((uint8_t *)t15) + 8);
                        gm_b16_8 t17;
                        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sgankvlme129s));
                        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t18 = gm_f_extend_0cs85gs(t16, t17);
                        gm_tftb1odc739t5 t19 = t18;
                        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 1, gm_srco4ljquuk5d87, 69, 25);
                        gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 1));
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 69, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t22 = gm_l4;
                    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                    uint64_t t25 = r23;
                    gm_l4 = t25;
                }
            }
lb4:;
        }
    }
    {
        void * t26 = gm_l0;
        void * t27 = (void *)(((uint8_t *)t26) + 8);
        gm_b16_8 t28 = gm_l2;
        gm_tftb1odc739t5 t29 = gm_f_extend_0cs85gs(t27, t28);
        gm_tftb1odc739t5 t30 = t29;
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srco4ljquuk5d87, 70, 5);
        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
    }
    {
        void * t32 = gm_l0;
        void * t33 = (void *)(((uint8_t *)t32) + 8);
        gm_tftb1odc739t5 t34 = gm_f_push_t83rg2m(t33, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t35 = t34;
        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_srco4ljquuk5d87, 71, 5);
        gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_loop_u0ja07u(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3) {
    struct gm_fr_write_loop_u0ja07u F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_b16_8 gm_l3; /* ty */
    void * gm_l4; /* st */
    GM_STACK_CHECK(&F, 436u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_tiilmfd16v3fc t4;
        memset((uint8_t *)&t4 + 4, 0, 4);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)a2) + 0));
        GM_ST(uint32_t, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 8, t6);
        gm_tabb710d9mq2e t7;
        gm_f_format_lde77u1((void *)&t7, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_srco4ljquuk5d87, 376, 25);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
        gm_b16_8 t10 = t9;
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        gm_b0_1 t12 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t2, t3, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = gm_l0;
        uint64_t t14 = gm_l1;
        uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 377, 19);
        uint64_t t17 = r15;
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
        gm_b16_8 t19 = t18;
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
        gm_b0_1 t21 = gm_f_write_conds_k9e57un((void *)NULL, cx, NULL, t13, t17, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t22 = gm_l0;
        uint64_t t23 = gm_l1;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 378, 18);
        uint64_t t26 = r24;
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sgk015tlfp2cu));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(4));
        gm_b0_1 t28 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t22, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t29 = gm_l0;
        uint64_t t30 = gm_l1;
        uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)2ULL), &r31);
        if (o32) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 379, 19);
        uint64_t t33 = r31;
        void * t34 = GM_LD(void *, (((uint8_t *)a2) + 24));
        gm_b0_1 t35 = gm_f_write_block_q5mpqjf((void *)NULL, cx, NULL, t29, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t37 = GM_LD(void *, (((uint8_t *)a2) + 32));
        void * t38 = t37;
        if (!(GM_LD(void *, ((uint8_t *)&t38)) != 0)) goto ie36;
        void * t39 = GM_LD(void *, ((uint8_t *)&t38));
        gm_l4 = t39;
        {
            void * t41 = gm_l0;
            uint64_t t42 = gm_l1;
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 381, 22);
            uint64_t t45 = r43;
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_si9h59pjqftat));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(4));
            gm_b0_1 t47 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t41, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t48 = gm_l0;
            uint64_t t49 = gm_l1;
            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)2ULL), &r50);
            if (o51) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 382, 22);
            uint64_t t52 = r50;
            void * t53 = gm_l4;
            gm_b0_1 t54 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t48, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix36;
ie36:;
ix36:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_pat_4qu9dcc(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_write_pat_4qu9dcc F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    void * gm_l2; /* p */
    gm_b16_8 gm_l3; /* ty */
    uint32_t gm_l4; /* l */
    void * gm_l5; /* x */
    uint8_t gm_l8; /* inclusive */
    void * gm_l9; /* q */
    void * gm_l10; /* q */
    void * gm_l11; /* q */
    gm_b16_8 gm_l12; /* name */
    uint32_t gm_l13; /* v */
    uint32_t gm_l14; /* v */
    void * gm_l15; /* q */
    gm_b16_8 gm_l16; /* fs */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l19; /* f */
    gm_b16_8 gm_l20; /* ps */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    void * gm_l23; /* q */
    gm_b16_8 gm_l24; /* head */
    GM_STACK_CHECK(&F, 2518u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t4) + 0));
        gm_b16_8 t6 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t3, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t6;
    }
    {
        void * t7 = gm_l2;
        gm_b16_8 t8;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        switch (t21) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma15; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma15; case ((uint8_t)8ULL): goto ma16; case ((uint8_t)9ULL): goto ma18; case ((uint8_t)10ULL): goto ma19; default: goto ma20; }
ma10:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t23 != ((uint8_t)0ULL)) goto mn22;
        gm_b16_8 t24;
        GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sbuhie0vb9t62));
        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(8));
        t8 = t24;
        goto me9;
mn22:;
ma11:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t26 != ((uint8_t)1ULL)) goto mn25;
        uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)t7) + 8) + 8));
        gm_l4 = t27;
        void * t28 = gm_l0;
        uint32_t t29 = gm_l4;
        gm_b16_8 t30 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t31;
        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
        gm_tabb710d9mq2e t32;
        gm_f_format_lpj4rro((void *)&t32, (void *)NULL, cx, NULL, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t33 = t32;
        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 8, gm_srco4ljquuk5d87, 530, 21);
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 8));
        gm_b16_8 t35 = t34;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        t8 = t36;
        goto me9;
mn25:;
ma12:;
        uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t38 != ((uint8_t)2ULL)) goto mn37;
        void * t39 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 8));
        gm_l5 = t39;
        {
            void * t41 = gm_l0;
            uint64_t t42 = gm_l1;
            gm_b16_8 t43 = gm_l3;
            gm_tnpifv20irnph t44;
            GM_ST(gm_b16_8, ((uint8_t *)&t44 + 0), t43);
            gm_tabb710d9mq2e t45;
            gm_f_format_8vmpj9s((void *)&t45, (void *)NULL, cx, NULL, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t46 = t45;
            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_srco4ljquuk5d87, 532, 33);
            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
            gm_b16_8 t48 = t47;
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            gm_b0_1 t50 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t41, t42, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t51 = gm_l0;
            uint64_t t52 = gm_l1;
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            if (o54) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 533, 26);
            uint64_t t55 = r53;
            void * t56 = gm_l5;
            gm_b0_1 t57 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t51, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b0_1 t58;
        uint8_t t59 = 0;
        F.ret = t58;
        goto gm_fret;
        goto me9;
mn37:;
ma13:;
        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t61 != ((uint8_t)3ULL)) goto mn60;
        gm_tbci5ospk5nnn t62 = GM_LD(gm_tbci5ospk5nnn, (((((uint8_t *)t7) + 8) + 8) + 0));
        F.l6 = t62;
        gm_tbci5ospk5nnn t63 = GM_LD(gm_tbci5ospk5nnn, (((((uint8_t *)t7) + 8) + 8) + 48));
        F.l7 = t63;
        uint8_t t64 = GM_LD(uint8_t, (((((uint8_t *)t7) + 8) + 8) + 96));
        gm_l8 = t64;
        gm_tfeab279c16rr t65;
        gm_b16_8 t66 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t65 + 0, t66);
        gm_b16_8 t67 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t65 + 16, t67);
        gm_b16_8 t68;
        uint8_t t70 = gm_l8;
        if (!t70) goto ie69;
        gm_b16_8 t72;
        GM_ST(void *, (uint8_t *)&t72, (void *)(gm_sksprenpp79ih));
        GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(10));
        gm_b16_8 t73 = t72;
        t68 = t73;
        goto ix69;
ie69:;
        gm_b16_8 t75;
        GM_ST(void *, (uint8_t *)&t75, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(0));
        gm_b16_8 t76 = t75;
        t68 = t76;
ix69:;
        GM_ST(gm_b16_8, (uint8_t *)&t65 + 32, t68);
        gm_tabb710d9mq2e t77;
        gm_f_format_6h8qssq((void *)&t77, (void *)NULL, cx, NULL, &t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t78 = t77;
        if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 8, gm_srco4ljquuk5d87, 536, 38);
        gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t78) + 8));
        gm_b16_8 t80 = t79;
        gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
        t8 = t81;
        goto me9;
mn60:;
ma14:;
        uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t83 != ((uint8_t)4ULL)) goto mn82;
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_sovel03lmfl8u));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(8));
        t8 = t84;
        goto me9;
mn82:;
ma15:;
        uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t88 != ((uint8_t)5ULL)) goto pa87;
        void * t89 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 8));
        gm_l9 = t89;
        goto po86;
pa87:;
        uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t91 != ((uint8_t)6ULL)) goto pa90;
        void * t92 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 8));
        gm_l9 = t92;
        goto po86;
pa90:;
        uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t94 != ((uint8_t)7ULL)) goto pa93;
        void * t95 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 8));
        gm_l9 = t95;
        goto po86;
pa93:;
        goto mn85;
po86:;
        {
            void * t97 = gm_l2;
            gm_b16_8 t98;
            uint8_t t104 = GM_LD(uint8_t, ((((uint8_t *)t97) + 8) + 0));
            switch (t104) { case ((uint8_t)0ULL): goto ma102; case ((uint8_t)1ULL): goto ma102; case ((uint8_t)2ULL): goto ma102; case ((uint8_t)3ULL): goto ma102; case ((uint8_t)4ULL): goto ma102; case ((uint8_t)5ULL): goto ma100; case ((uint8_t)6ULL): goto ma101; case ((uint8_t)7ULL): goto ma102; case ((uint8_t)8ULL): goto ma102; case ((uint8_t)9ULL): goto ma102; case ((uint8_t)10ULL): goto ma102; default: goto ma102; }
ma100:;
            uint8_t t106 = GM_LD(uint8_t, ((((uint8_t *)t97) + 8) + 0));
            if (t106 != ((uint8_t)5ULL)) goto mn105;
            gm_b16_8 t107;
            GM_ST(void *, (uint8_t *)&t107, (void *)(gm_sjourjtj9e3to));
            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(8));
            t98 = t107;
            goto me99;
mn105:;
ma101:;
            uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t97) + 8) + 0));
            if (t109 != ((uint8_t)6ULL)) goto mn108;
            gm_b16_8 t110;
            GM_ST(void *, (uint8_t *)&t110, (void *)(gm_se0a26478itki));
            GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(6));
            t98 = t110;
            goto me99;
mn108:;
ma102:;
            gm_b16_8 t112;
            GM_ST(void *, (uint8_t *)&t112, (void *)(gm_s2iprgandb78l));
            GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(7));
            t98 = t112;
            goto me99;
mn111:;
ma103:;
            gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 539, 21);
me99:;
            gm_l12 = t98;
        }
        {
            void * t113 = gm_l0;
            uint64_t t114 = gm_l1;
            gm_t6j6smj7qoctu t115;
            gm_b16_8 t116 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t115 + 0, t116);
            gm_b16_8 t117 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t115 + 16, t117);
            gm_tabb710d9mq2e t118;
            gm_f_format_r0u4lpe((void *)&t118, (void *)NULL, cx, NULL, &t115);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t119 = t118;
            if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 8, gm_srco4ljquuk5d87, 544, 33);
            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&t119) + 8));
            gm_b16_8 t121 = t120;
            gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
            gm_b0_1 t123 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t113, t114, t122);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t124 = gm_l0;
            uint64_t t125 = gm_l1;
            uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
            if (o127) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 545, 25);
            uint64_t t128 = r126;
            void * t129 = gm_l9;
            gm_b0_1 t130 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t124, t128, t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b0_1 t131;
        uint8_t t132 = 0;
        F.ret = t131;
        goto gm_fret;
        goto me9;
mn85:;
ma16:;
        uint8_t t134 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t134 != ((uint8_t)8ULL)) goto mn133;
        uint32_t t135 = GM_LD(uint32_t, (((((uint8_t *)t7) + 8) + 8) + 0));
        gm_l13 = t135;
        if ((GM_LD(void *, (((((uint8_t *)t7) + 8) + 8) + 8)) != 0)) goto mn133;
        uint32_t t136 = gm_l13;
        gm_t88rcucg1b7uk t137;
        GM_ST(uint32_t, ((uint8_t *)&t137 + 0), t136);
        gm_tabb710d9mq2e t138;
        gm_f_format_3669u6h((void *)&t138, (void *)NULL, cx, NULL, t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t139 = t138;
        if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 8, gm_srco4ljquuk5d87, 548, 30);
        gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&t139) + 8));
        gm_b16_8 t141 = t140;
        gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
        t8 = t142;
        goto me9;
mn133:;
ma17:;
        uint8_t t144 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t144 != ((uint8_t)8ULL)) goto mn143;
        uint32_t t145 = GM_LD(uint32_t, (((((uint8_t *)t7) + 8) + 8) + 0));
        gm_l14 = t145;
        if (!(GM_LD(void *, (((((uint8_t *)t7) + 8) + 8) + 8)) != 0)) goto mn143;
        void * t146 = GM_LD(void *, (((((uint8_t *)t7) + 8) + 8) + 8));
        gm_l15 = t146;
        {
            void * t148 = gm_l0;
            uint64_t t149 = gm_l1;
            gm_tiilmfd16v3fc t150;
            memset((uint8_t *)&t150 + 4, 0, 4);
            uint32_t t151 = gm_l14;
            GM_ST(uint32_t, (uint8_t *)&t150 + 0, t151);
            gm_b16_8 t152 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t150 + 8, t152);
            gm_tabb710d9mq2e t153;
            gm_f_format_p7ogfqk((void *)&t153, (void *)NULL, cx, NULL, &t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t154 = t153;
            if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 8, gm_srco4ljquuk5d87, 550, 33);
            gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&t154) + 8));
            gm_b16_8 t156 = t155;
            gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
            gm_b0_1 t158 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t148, t149, t157);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t159 = gm_l0;
            uint64_t t160 = gm_l1;
            uint64_t r161; bool o162 = __builtin_add_overflow(t160, ((uint64_t)1ULL), &r161);
            if (o162) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 551, 25);
            uint64_t t163 = r161;
            void * t164 = gm_l15;
            gm_b0_1 t165 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t159, t163, t164);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b0_1 t166;
        uint8_t t167 = 0;
        F.ret = t166;
        goto gm_fret;
        goto me9;
mn143:;
ma18:;
        uint8_t t169 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t169 != ((uint8_t)9ULL)) goto mn168;
        gm_b16_8 t170 = GM_LD(gm_b16_8, ((((uint8_t *)t7) + 8) + 8));
        gm_l16 = t170;
        {
            void * t172 = gm_l0;
            uint64_t t173 = gm_l1;
            gm_b16_8 t174 = gm_l3;
            gm_tnpifv20irnph t175;
            GM_ST(gm_b16_8, ((uint8_t *)&t175 + 0), t174);
            gm_tabb710d9mq2e t176;
            gm_f_format_a4vi4f7((void *)&t176, (void *)NULL, cx, NULL, t175);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t177 = t176;
            if (((((uint8_t *)&t177))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t177) + 8, gm_srco4ljquuk5d87, 555, 33);
            gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&t177) + 8));
            gm_b16_8 t179 = t178;
            gm_b16_8 t180 = GM_LD(gm_b16_8, ((uint8_t *)&t179));
            gm_b0_1 t181 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t172, t173, t180);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t183 = gm_l16;
                gm_l17 = t183;
            }
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t185 = gm_l18;
                    uint64_t t186 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    uint8_t t187 = (t185 < t186);
                    if (!t187) goto lb184;
                    {
                        uint64_t t190 = gm_l18;
                        uint64_t t191 = (uintptr_t)t190;
                        uint64_t t192 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                        gm_ttggq7ng4kp3p t195 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t191 * 16));
                        gm_l19 = t195;
                        {
                            void * t197 = gm_l0;
                            uint64_t t198 = gm_l1;
                            uint64_t r199; bool o200 = __builtin_add_overflow(t198, ((uint64_t)1ULL), &r199);
                            if (o200) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 557, 30);
                            uint64_t t201 = r199;
                            uint32_t t202 = GM_LD(uint32_t, (((uint8_t *)&gm_l19) + 0));
                            gm_t88rcucg1b7uk t203;
                            GM_ST(uint32_t, ((uint8_t *)&t203 + 0), t202);
                            gm_tabb710d9mq2e t204;
                            gm_f_format_208te7b((void *)&t204, (void *)NULL, cx, NULL, t203);
                            if (gm_unw.kind) {
                                goto gm_ll188;
                            }
                            gm_tabb710d9mq2e t205 = t204;
                            if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 8, gm_srco4ljquuk5d87, 557, 41);
                            gm_b16_8 t206 = GM_LD(gm_b16_8, (((uint8_t *)&t205) + 8));
                            gm_b16_8 t207 = t206;
                            gm_b16_8 t208 = GM_LD(gm_b16_8, ((uint8_t *)&t207));
                            gm_b0_1 t209 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t197, t201, t208);
                            if (gm_unw.kind) {
                                goto gm_ll188;
                            }
                        }
                        {
                            void * t210 = gm_l0;
                            uint64_t t211 = gm_l1;
                            uint64_t r212; bool o213 = __builtin_add_overflow(t211, ((uint64_t)2ULL), &r212);
                            if (o213) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 558, 29);
                            uint64_t t214 = r212;
                            void * t215 = GM_LD(void *, (((uint8_t *)&gm_l19) + 8));
                            gm_b0_1 t216 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t210, t214, t215);
                            if (gm_unw.kind) {
                                goto gm_ll188;
                            }
                        }
                        goto me193;
mn194:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 556, 13);
me193:;
                    }
lc184:;
                    {
                        uint64_t t218 = gm_l18;
                        uint64_t r219; bool o220 = __builtin_add_overflow(t218, ((uint64_t)1ULL), &r219);
                        uint64_t t221 = r219;
                        gm_l18 = t221;
                    }
                }
gm_ll188:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb184; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc184; }
                }
                goto gm_lad;
lb184:;
            }
        }
        gm_b0_1 t222;
        uint8_t t223 = 0;
        F.ret = t222;
        goto gm_fret;
        goto me9;
mn168:;
ma19:;
        uint8_t t225 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t225 != ((uint8_t)10ULL)) goto mn224;
        gm_b16_8 t226 = GM_LD(gm_b16_8, ((((uint8_t *)t7) + 8) + 8));
        gm_l20 = t226;
        {
            void * t228 = gm_l0;
            uint64_t t229 = gm_l1;
            gm_b16_8 t230 = gm_l3;
            gm_tnpifv20irnph t231;
            GM_ST(gm_b16_8, ((uint8_t *)&t231 + 0), t230);
            gm_tabb710d9mq2e t232;
            gm_f_format_6b7mjf6((void *)&t232, (void *)NULL, cx, NULL, t231);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t233 = t232;
            if (((((uint8_t *)&t233))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t233) + 8, gm_srco4ljquuk5d87, 563, 33);
            gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)&t233) + 8));
            gm_b16_8 t235 = t234;
            gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
            gm_b0_1 t237 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t228, t229, t236);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t239 = gm_l20;
                gm_l21 = t239;
            }
            {
                gm_l22 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t241 = gm_l22;
                    uint64_t t242 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    uint8_t t243 = (t241 < t242);
                    if (!t243) goto lb240;
                    {
                        uint64_t t246 = gm_l22;
                        uint64_t t247 = (uintptr_t)t246;
                        uint64_t t248 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                        void * t249 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t247 * 8));
                        void * t250 = t249;
                        void * t253 = GM_LD(void *, ((uint8_t *)&t250));
                        gm_l23 = t253;
                        {
                            void * t255 = gm_l0;
                            uint64_t t256 = gm_l1;
                            uint64_t r257; bool o258 = __builtin_add_overflow(t256, ((uint64_t)1ULL), &r257);
                            if (o258) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 564, 39);
                            uint64_t t259 = r257;
                            void * t260 = gm_l23;
                            gm_b0_1 t261 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t255, t259, t260);
                            if (gm_unw.kind) {
                                goto gm_ll244;
                            }
                        }
                        goto me251;
mn252:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 564, 13);
me251:;
                    }
lc240:;
                    {
                        uint64_t t263 = gm_l22;
                        uint64_t r264; bool o265 = __builtin_add_overflow(t263, ((uint64_t)1ULL), &r264);
                        uint64_t t266 = r264;
                        gm_l22 = t266;
                    }
                }
gm_ll244:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb240; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc240; }
                }
                goto gm_lad;
lb240:;
            }
        }
        gm_b0_1 t267;
        uint8_t t268 = 0;
        F.ret = t267;
        goto gm_fret;
        goto me9;
mn224:;
ma20:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 528, 20);
me9:;
        gm_l24 = t8;
    }
    {
        void * t269 = gm_l0;
        uint64_t t270 = gm_l1;
        gm_t6j6smj7qoctu t271;
        gm_b16_8 t272 = gm_l24;
        GM_ST(gm_b16_8, (uint8_t *)&t271 + 0, t272);
        gm_b16_8 t273 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t271 + 16, t273);
        gm_tabb710d9mq2e t274;
        gm_f_format_r0u4lpe((void *)&t274, (void *)NULL, cx, NULL, &t271);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t275 = t274;
        if (((((uint8_t *)&t275))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t275) + 8, gm_srco4ljquuk5d87, 568, 25);
        gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)&t275) + 8));
        gm_b16_8 t277 = t276;
        gm_b16_8 t278 = GM_LD(gm_b16_8, ((uint8_t *)&t277));
        gm_b0_1 t279 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t269, t270, t278);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_write_text_h6e1ibp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_write_text_h6e1ibp F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* p */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* spec */
    GM_STACK_CHECK(&F, 500u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        void * t3 = gm_l0;
        gm_b16_8 t4 = gm_f_sink_text_s4o8tr0((void *)NULL, cx, NULL, t3, &(*(gm_th1g69d7vpipg *)(((uint8_t *)a1) + 0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tftb1odc739t5 t6 = gm_f_write_uebhmoa(t2, t5);
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srco4ljquuk5d87, 389, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 40));
            gm_l3 = t10;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t12 = gm_l4;
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l4;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t18 * 16));
                    gm_l5 = t22;
                    {
                        void * t24 = (void *)((uint8_t *)&F.l2);
                        gm_tftb1odc739t5 t25 = gm_f_push_t83rg2m(t24, ((uint8_t)32ULL));
                        gm_tftb1odc739t5 t26 = t25;
                        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srco4ljquuk5d87, 391, 9);
                        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
                    }
                    {
                        gm_b16_8 t28 = gm_l5;
                        void * t29 = (void *)((uint8_t *)&F.l2);
                        gm_b0_1 t30 = gm_f_escape_bytes_25mo226((void *)NULL, cx, NULL, t28, t29);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 390, 5);
me20:;
                }
lc11:;
                {
                    uint64_t t32 = gm_l4;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l4 = t35;
                }
            }
gm_ll15:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
            }
            goto gm_lad;
lb11:;
        }
    }
    {
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)a1) + 56) + 8);
            gm_l6 = t37;
        }
        {
            for (;;) {
                uint64_t t39 = gm_l7;
                uint64_t t40 = gm_l6;
                uint8_t t41 = (t39 < t40);
                if (!t41) goto lb38;
                {
                    uint64_t t44 = gm_l7;
                    uint64_t t45 = t44;
                    uint64_t t48 = GM_LD(uint64_t, ((uint8_t *)&t45));
                    gm_l8 = t48;
                    {
                        uint64_t t50 = gm_l8;
                        uint64_t t51 = (uintptr_t)t50;
                        uint64_t t52 = GM_LD(uintptr_t, (((uint8_t *)a1) + 56) + 8);
                        if (t51 >= (uintptr_t)(t52)) gm_trap_index(t51, t52, gm_srco4ljquuk5d87, 395, 18);
                        void * t53 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)a1) + 56)) + t51 * 24);
                        gm_l9 = t53;
                    }
                    {
                        void * t55 = gm_l9;
                        uint8_t t56 = gm_f_is_default_spec_cdd8jng(t55);
                        if (!t56) goto ie54;
                        {
                            void * t58 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t59;
                            GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sjeopa747clpb));
                            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(3));
                            gm_tftb1odc739t5 t60 = gm_f_extend_0cs85gs(t58, t59);
                            gm_tftb1odc739t5 t61 = t60;
                            if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_srco4ljquuk5d87, 397, 13);
                            gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
                        }
                        goto ix54;
ie54:;
                        {
                            void * t64 = (void *)((uint8_t *)&F.l2);
                            gm_tftb1odc739t5 t65 = gm_f_push_t83rg2m(t64, ((uint8_t)32ULL));
                            gm_tftb1odc739t5 t66 = t65;
                            if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srco4ljquuk5d87, 399, 13);
                            gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
                        }
                        {
                            void * t68 = gm_l9;
                            void * t69 = (void *)((uint8_t *)&F.l2);
                            gm_b0_1 t70 = gm_f_spec_text_fr9884d((void *)NULL, cx, NULL, t68, t69);
                            if (gm_unw.kind) {
                                goto gm_ll42;
                            }
                        }
ix54:;
                    }
                    goto me46;
mn47:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 394, 5);
me46:;
                }
lc38:;
                {
                    uint64_t t72 = gm_l7;
                    uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                    uint64_t t75 = r73;
                    gm_l7 = t75;
                }
            }
gm_ll42:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb38; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc38; }
            }
            goto gm_lad;
lb38:;
        }
    }
    {
        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t77 = t76;
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_with_j37nil7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3) {
    struct gm_fr_write_with_j37nil7 F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_b16_8 gm_l3; /* ty */
    void * gm_l4; /* s */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t4lnf71mruqpt gm_l7; /* h */
    void * gm_l8; /* x */
    gm_b16_8 gm_l9; /* decls */
    void * gm_l10; /* x */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint32_t gm_l14; /* i */
    void * gm_l16; /* x */
    GM_STACK_CHECK(&F, 1404u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = gm_l1;
        gm_tiilmfd16v3fc t6;
        memset((uint8_t *)&t6 + 4, 0, 4);
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)a2) + 0));
        GM_ST(uint32_t, (uint8_t *)&t6 + 0, t7);
        gm_b16_8 t8 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 8, t8);
        gm_tabb710d9mq2e t9;
        gm_f_format_gnbipfi((void *)&t9, (void *)NULL, cx, NULL, &t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_srco4ljquuk5d87, 336, 25);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
        gm_b16_8 t12 = t11;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        gm_b0_1 t14 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t4, t5, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
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
                    gm_t4lnf71mruqpt t28 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t24 * 32));
                    gm_l7 = t28;
                    {
                        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t32 != ((uint8_t)0ULL)) goto mn31;
                        void * t33 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                        gm_l8 = t33;
                        {
                            void * t35 = gm_l0;
                            uint64_t t36 = gm_l1;
                            uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                            if (o38) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 340, 30);
                            uint64_t t39 = r37;
                            gm_b16_8 t40;
                            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sic5vsema0r36));
                            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(7));
                            gm_b0_1 t41 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t35, t39, t40);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                        }
                        {
                            void * t42 = gm_l0;
                            uint64_t t43 = gm_l1;
                            uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)2ULL), &r44);
                            if (o45) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 341, 30);
                            uint64_t t46 = r44;
                            void * t47 = gm_l8;
                            gm_b0_1 t48 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t42, t46, t47);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                        }
                        goto me30;
mn31:;
                        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t50 != ((uint8_t)1ULL)) goto mn49;
                        gm_b16_8 t51 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l7) + 8) + 0));
                        gm_l9 = t51;
                        void * t52 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 16));
                        gm_l10 = t52;
                        {
                            gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                        }
                        {
                            {
                                gm_b16_8 t55 = gm_l9;
                                gm_l12 = t55;
                            }
                            {
                                gm_l13 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t57 = gm_l13;
                                    uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                    uint8_t t59 = (t57 < t58);
                                    if (!t59) goto lb56;
                                    {
                                        uint64_t t62 = gm_l13;
                                        uint64_t t63 = (uintptr_t)t62;
                                        uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                        uint32_t t65 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t63 * 4));
                                        uint32_t t66 = t65;
                                        uint32_t t69 = GM_LD(uint32_t, ((uint8_t *)&t66));
                                        gm_l14 = t69;
                                        {
                                            uint32_t t71 = gm_l14;
                                            uint64_t t72 = (uint64_t)t71;
                                            uint64_t t73 = (uintptr_t)t72;
                                            void * t74 = gm_l4;
                                            uint64_t t75 = GM_LD(uintptr_t, ((((uint8_t *)t74) + 544) + 0) + 8);
                                            if (t73 >= (uintptr_t)(t75)) gm_trap_index(t73, t75, gm_srco4ljquuk5d87, 346, 27);
                                            gm_tvc83pqnepc2e t76 = GM_LD(gm_tvc83pqnepc2e, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t74) + 544) + 0)) + t73 * 64));
                                            F.l15 = t76;
                                        }
                                        {
                                            void * t77 = (void *)((uint8_t *)&F.l11);
                                            gm_t6j6smj7qoctu t78;
                                            void * t79 = gm_l4;
                                            void * t80 = (void *)(((uint8_t *)t79) + 8);
                                            void * t81 = (void *)(((uint8_t *)&F.l15) + 0);
                                            gm_b16_8 t82 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t80, t81);
                                            if (gm_unw.kind) {
                                                goto gm_ll60;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t78 + 0, t82);
                                            void * t83 = gm_l4;
                                            gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 24));
                                            gm_b16_8 t85 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t83, t84);
                                            if (gm_unw.kind) {
                                                goto gm_ll60;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t78 + 16, t85);
                                            gm_tabb710d9mq2e t86;
                                            gm_f_format_3aao56v((void *)&t86, (void *)NULL, cx, NULL, &t78);
                                            if (gm_unw.kind) {
                                                goto gm_ll60;
                                            }
                                            gm_tabb710d9mq2e t87 = t86;
                                            if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 8, gm_srco4ljquuk5d87, 347, 32);
                                            gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&t87) + 8));
                                            gm_b16_8 t89 = t88;
                                            gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                                            gm_tftb1odc739t5 t91 = gm_f_push_3ef746b(t77, t90);
                                            gm_tftb1odc739t5 t92 = t91;
                                            if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srco4ljquuk5d87, 347, 21);
                                            gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
                                        }
                                        goto me67;
mn68:;
                                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 345, 17);
me67:;
                                    }
lc56:;
                                    {
                                        uint64_t t95 = gm_l13;
                                        uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                                        uint64_t t98 = r96;
                                        gm_l13 = t98;
                                    }
                                }
gm_ll60:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb56; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc56; }
                                }
                                goto gm_ll21;
lb56:;
                            }
                        }
                        {
                            void * t99 = gm_l0;
                            uint64_t t100 = gm_l1;
                            uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                            if (o102) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 349, 30);
                            uint64_t t103 = r101;
                            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                            gm_b16_8 t105 = t104;
                            gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                            gm_b16_8 t107;
                            GM_ST(void *, (uint8_t *)&t107, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(2));
                            gm_tabb710d9mq2e t108;
                            gm_f_join_sfknljb((void *)&t108, (void *)NULL, cx, NULL, t106, t107);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            gm_tabb710d9mq2e t109 = t108;
                            if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 8, gm_srco4ljquuk5d87, 349, 67);
                            gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
                            gm_tnpifv20irnph t111;
                            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 0), t110);
                            gm_tabb710d9mq2e t112;
                            gm_f_format_i37o6qg((void *)&t112, (void *)NULL, cx, NULL, t111);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            gm_tabb710d9mq2e t113 = t112;
                            if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 8, gm_srco4ljquuk5d87, 349, 41);
                            gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&t113) + 8));
                            gm_b16_8 t115 = t114;
                            gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
                            gm_b0_1 t117 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t99, t103, t116);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                        }
                        {
                            void * t118 = gm_l0;
                            uint64_t t119 = gm_l1;
                            uint64_t r120; bool o121 = __builtin_add_overflow(t119, ((uint64_t)2ULL), &r120);
                            if (o121) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 350, 30);
                            uint64_t t122 = r120;
                            void * t123 = gm_l10;
                            gm_b0_1 t124 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t118, t122, t123);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                        }
                        goto me30;
mn49:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 338, 9);
me30:;
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 337, 5);
me26:;
                }
lc17:;
                {
                    uint64_t t126 = gm_l6;
                    uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                    uint64_t t129 = r127;
                    gm_l6 = t129;
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
        void * t131 = GM_LD(void *, (((uint8_t *)a2) + 24));
        void * t132 = t131;
        if (!(GM_LD(void *, ((uint8_t *)&t132)) != 0)) goto ie130;
        void * t133 = GM_LD(void *, ((uint8_t *)&t132));
        gm_l16 = t133;
        {
            void * t135 = gm_l0;
            uint64_t t136 = gm_l1;
            uint64_t r137; bool o138 = __builtin_add_overflow(t136, ((uint64_t)1ULL), &r137);
            if (o138) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 355, 22);
            uint64_t t139 = r137;
            gm_b16_8 t140;
            GM_ST(void *, (uint8_t *)&t140, (void *)(gm_sdl8mgtvebfcs));
            GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(2));
            gm_b0_1 t141 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t135, t139, t140);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t142 = gm_l0;
            uint64_t t143 = gm_l1;
            uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)2ULL), &r144);
            if (o145) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 356, 22);
            uint64_t t146 = r144;
            void * t147 = gm_l16;
            gm_b0_1 t148 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t142, t146, t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix130;
ie130:;
ix130:;
    }
    {
        void * t149 = gm_l0;
        uint64_t t150 = gm_l1;
        uint64_t r151; bool o152 = __builtin_add_overflow(t150, ((uint64_t)1ULL), &r151);
        if (o152) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 358, 18);
        uint64_t t153 = r151;
        gm_b16_8 t154;
        GM_ST(void *, (uint8_t *)&t154, (void *)(gm_sgk015tlfp2cu));
        GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(4));
        gm_b0_1 t155 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t149, t153, t154);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t156 = gm_l0;
        uint64_t t157 = gm_l1;
        uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)2ULL), &r158);
        if (o159) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 359, 18);
        uint64_t t160 = r158;
        void * t161 = GM_LD(void *, (((uint8_t *)a2) + 32));
        gm_b0_1 t162 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t156, t160, t161);
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
