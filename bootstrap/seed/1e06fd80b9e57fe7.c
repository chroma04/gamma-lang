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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_tj2m9dbh1ekv6 f0; uint8_t f1; uint8_t pe[3]; } gm_t46lmulm8gaj3;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; } gm_tk22flr9mh3ql;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; void * f3; } gm_tl93vjpcesj48;
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_tiilmfd16v3fc f0; gm_b16_8 f1; } gm_t1cs2fjnrqap1;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; gm_tkn42u7na3t81 v1; } f1; } gm_t1qr0p6kln6tf;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t46lmulm8gaj3) == 12 && _Alignof(gm_t46lmulm8gaj3) == 4 && offsetof(gm_t46lmulm8gaj3, f0) == 0 && offsetof(gm_t46lmulm8gaj3, f1) == 8, "layout of `?IntSuffix`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?FloatSuffix`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; void * v1; } f1; } gm_tapdr2ag17fh7;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_tiilmfd16v3fc f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tj44oivvogvtl;
_Static_assert(sizeof(gm_tk22flr9mh3ql) == 3 && _Alignof(gm_tk22flr9mh3ql) == 1 && offsetof(gm_tk22flr9mh3ql, f0) == 0 && offsetof(gm_tk22flr9mh3ql, f1) == 1 && offsetof(gm_tk22flr9mh3ql, f2) == 2, "layout of `(Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl93vjpcesj48) == 48 && _Alignof(gm_tl93vjpcesj48) == 8 && offsetof(gm_tl93vjpcesj48, f0) == 0 && offsetof(gm_tl93vjpcesj48, f1) == 8 && offsetof(gm_tl93vjpcesj48, f2) == 24 && offsetof(gm_tl93vjpcesj48, f3) == 40, "layout of `NestedFn`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t pe[4]; } gm_tuanp0jbrlb6j;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
typedef struct { gm_tlivgedjs5ps3 f0; gm_b16_8 f1; gm_tapdr2ag17fh7 f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t0kv05e068jrn;
_Static_assert(sizeof(gm_t1cs2fjnrqap1) == 40 && _Alignof(gm_t1cs2fjnrqap1) == 8 && offsetof(gm_t1cs2fjnrqap1, f0) == 0 && offsetof(gm_t1cs2fjnrqap1, f1) == 24, "layout of `HandlerLit`");
_Static_assert(sizeof(gm_t1qr0p6kln6tf) == 12 && _Alignof(gm_t1qr0p6kln6tf) == 4 && offsetof(gm_t1qr0p6kln6tf, f0) == 0 && offsetof(gm_t1qr0p6kln6tf, f1) == 4, "layout of `LabelValue`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t f2; uint8_t p3[3]; gm_t1qr0p6kln6tf f3; uint8_t pe[4]; } gm_t8enl5g7b22jb;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_t46lmulm8gaj3 f1; uint8_t f2; uint8_t pe[3]; } gm_ta42iqot1auae;
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tapdr2ag17fh7) == 48 && _Alignof(gm_tapdr2ag17fh7) == 8 && offsetof(gm_tapdr2ag17fh7, f0) == 0 && offsetof(gm_tapdr2ag17fh7, f1) == 8, "layout of `WhenElse`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tj44oivvogvtl) == 40 && _Alignof(gm_tj44oivvogvtl) == 8 && offsetof(gm_tj44oivvogvtl, f0) == 0 && offsetof(gm_tj44oivvogvtl, f1) == 24, "layout of `Performed`");
typedef struct { gm_tuanp0jbrlb6j f0; uint8_t f1; uint8_t pe[7]; } gm_tjvtgq0p0sehv;
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tuanp0jbrlb6j) == 32 && _Alignof(gm_tuanp0jbrlb6j) == 8 && offsetof(gm_tuanp0jbrlb6j, f0) == 0 && offsetof(gm_tuanp0jbrlb6j, f1) == 24, "layout of `ForLabel`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
_Static_assert(sizeof(gm_t0kv05e068jrn) == 152 && _Alignof(gm_t0kv05e068jrn) == 8 && offsetof(gm_t0kv05e068jrn, f0) == 0 && offsetof(gm_t0kv05e068jrn, f1) == 72 && offsetof(gm_t0kv05e068jrn, f2) == 88 && offsetof(gm_t0kv05e068jrn, f3) == 136, "layout of `FnLit`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t8enl5g7b22jb) == 48 && _Alignof(gm_t8enl5g7b22jb) == 8 && offsetof(gm_t8enl5g7b22jb, f0) == 0 && offsetof(gm_t8enl5g7b22jb, f1) == 24 && offsetof(gm_t8enl5g7b22jb, f2) == 28 && offsetof(gm_t8enl5g7b22jb, f3) == 32, "layout of `LabelInfo`");
typedef struct { gm_th1g69d7vpipg f0; void * f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_t9i38ih07bojq;
_Static_assert(sizeof(gm_ta42iqot1auae) == 64 && _Alignof(gm_ta42iqot1auae) == 8 && offsetof(gm_ta42iqot1auae, f0) == 0 && offsetof(gm_ta42iqot1auae, f1) == 48 && offsetof(gm_ta42iqot1auae, f2) == 60, "layout of `IntLit`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
typedef struct { gm_t8enl5g7b22jb f0; uint8_t f1; uint8_t pe[7]; } gm_te5gbhu3us3k4;
typedef struct { gm_tngtuiuskn938 f0; gm_taari1s0m51a7 f1; uint8_t f2; uint8_t pe[5]; } gm_tghmb53v8qi1e;
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tjvtgq0p0sehv) == 40 && _Alignof(gm_tjvtgq0p0sehv) == 8 && offsetof(gm_tjvtgq0p0sehv, f0) == 0 && offsetof(gm_tjvtgq0p0sehv, f1) == 32, "layout of `?ForLabel`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_tkn42u7na3t81 f3; uint8_t p4[4]; gm_tjvtgq0p0sehv f4; gm_tkn42u7na3t81 f5; gm_tqu3m2l3n8mnc f6; } gm_tnqmlaad6uchl;
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { gm_tnqmlaad6uchl f0; uint8_t f1; uint8_t pe[7]; } gm_t403eokj7ifg9;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; gm_tngtuiuskn938 v2; gm_tu15p0hi2qlv6 v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; gm_tntn2tf15puib v53; gm_t9i38ih07bojq v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_t5brlu7fal70j;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ta42iqot1auae v0; gm_tghmb53v8qi1e v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t6e9d80inoppc;
_Static_assert(sizeof(gm_t9i38ih07bojq) == 64 && _Alignof(gm_t9i38ih07bojq) == 8 && offsetof(gm_t9i38ih07bojq, f0) == 0 && offsetof(gm_t9i38ih07bojq, f1) == 40 && offsetof(gm_t9i38ih07bojq, f2) == 48 && offsetof(gm_t9i38ih07bojq, f3) == 56, "layout of `WriteDeferredExpr`");
_Static_assert(sizeof(gm_te5gbhu3us3k4) == 56 && _Alignof(gm_te5gbhu3us3k4) == 8 && offsetof(gm_te5gbhu3us3k4, f0) == 0 && offsetof(gm_te5gbhu3us3k4, f1) == 48, "layout of `?LabelInfo`");
_Static_assert(sizeof(gm_tghmb53v8qi1e) == 104 && _Alignof(gm_tghmb53v8qi1e) == 8 && offsetof(gm_tghmb53v8qi1e, f0) == 0 && offsetof(gm_tghmb53v8qi1e, f1) == 96 && offsetof(gm_tghmb53v8qi1e, f2) == 98, "layout of `FloatLit`");
_Static_assert(sizeof(gm_tnqmlaad6uchl) == 112 && _Alignof(gm_tnqmlaad6uchl) == 8 && offsetof(gm_tnqmlaad6uchl, f0) == 0 && offsetof(gm_tnqmlaad6uchl, f1) == 4 && offsetof(gm_tnqmlaad6uchl, f2) == 12 && offsetof(gm_tnqmlaad6uchl, f3) == 20 && offsetof(gm_tnqmlaad6uchl, f4) == 32 && offsetof(gm_tnqmlaad6uchl, f5) == 72 && offsetof(gm_tnqmlaad6uchl, f6) == 80, "layout of `ClauseInfo`");
typedef struct { gm_t1s75i08qtptd f0; gm_t0d02142k4tu8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tnrgict8sthcv;
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t403eokj7ifg9) == 120 && _Alignof(gm_t403eokj7ifg9) == 8 && offsetof(gm_t403eokj7ifg9, f0) == 0 && offsetof(gm_t403eokj7ifg9, f1) == 112, "layout of `?ClauseInfo`");
_Static_assert(sizeof(gm_t5brlu7fal70j) == 200 && _Alignof(gm_t5brlu7fal70j) == 8 && offsetof(gm_t5brlu7fal70j, f0) == 0 && offsetof(gm_t5brlu7fal70j, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t6e9d80inoppc) == 112 && _Alignof(gm_t6e9d80inoppc) == 8 && offsetof(gm_t6e9d80inoppc, f0) == 0 && offsetof(gm_t6e9d80inoppc, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t6e9d80inoppc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tfbvnc90lci8r;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t5brlu7fal70j f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tfpf0s89fpb3r;
_Static_assert(sizeof(gm_tnrgict8sthcv) == 120 && _Alignof(gm_tnrgict8sthcv) == 8 && offsetof(gm_tnrgict8sthcv, f0) == 0 && offsetof(gm_tnrgict8sthcv, f1) == 40 && offsetof(gm_tnrgict8sthcv, f2) == 104, "layout of `PatArg`");
typedef struct { gm_t08olq97rscn3 f0; gm_tfbvnc90lci8r f1; } gm_t2tlhjbsqg5eb;
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
_Static_assert(sizeof(gm_tfbvnc90lci8r) == 136 && _Alignof(gm_tfbvnc90lci8r) == 8 && offsetof(gm_tfbvnc90lci8r, f0) == 0 && offsetof(gm_tfbvnc90lci8r, f1) == 8 && offsetof(gm_tfbvnc90lci8r, f2) == 120, "layout of `Expr`");
_Static_assert(sizeof(gm_tfpf0s89fpb3r) == 224 && _Alignof(gm_tfpf0s89fpb3r) == 8 && offsetof(gm_tfpf0s89fpb3r, f0) == 0 && offsetof(gm_tfpf0s89fpb3r, f1) == 8 && offsetof(gm_tfpf0s89fpb3r, f2) == 208, "layout of `Expr`");
typedef struct { gm_tfbvnc90lci8r f0; uint8_t f1; uint8_t pe[7]; } gm_tj2dlstgqla1c;
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_t2tlhjbsqg5eb) == 168 && _Alignof(gm_t2tlhjbsqg5eb) == 8 && offsetof(gm_t2tlhjbsqg5eb, f0) == 0 && offsetof(gm_t2tlhjbsqg5eb, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_tj2dlstgqla1c) == 144 && _Alignof(gm_tj2dlstgqla1c) == 8 && offsetof(gm_tj2dlstgqla1c, f0) == 0 && offsetof(gm_tj2dlstgqla1c, f1) == 136, "layout of `?Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tfbvnc90lci8r v0; gm_t2tlhjbsqg5eb v1; gm_tfbvnc90lci8r v2; } f1; } gm_tq6gu4l68u3rv;
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
typedef struct { gm_tq6gu4l68u3rv f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tpff59ptr3s92;
_Static_assert(sizeof(gm_tq6gu4l68u3rv) == 176 && _Alignof(gm_tq6gu4l68u3rv) == 8 && offsetof(gm_tq6gu4l68u3rv, f0) == 0 && offsetof(gm_tq6gu4l68u3rv, f1) == 8, "layout of `ArgKind`");
_Static_assert(sizeof(gm_tpff59ptr3s92) == 192 && _Alignof(gm_tpff59ptr3s92) == 8 && offsetof(gm_tpff59ptr3s92, f0) == 0 && offsetof(gm_tpff59ptr3s92, f1) == 176, "layout of `Arg`");
struct gm_fr_enumerate_loop_5f6m89f {
    gm_frame h;
    gm_tsth9qu1dv0g3 l5; /* callee */
    gm_tl874t2atnlh3 l12; /* span */
    gm_trqfvmjtt4sij l17; /* fields */
    gm_trqfvmjtt4sij l20; /* stmts */
    void * ret;
};
struct gm_fr_fn_t9suqla {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_iterator_cc5qo7n {
    gm_frame h;
    gm_tsth9qu1dv0g3 l11; /* callee */
    gm_tddtjfggdat0d l14; /* sig */
    gm_tddtjfggdat0d l15; /* sig */
    gm_trqfvmjtt4sij l16; /* yields */
    gm_trqfvmjtt4sij l27; /* pre */
    gm_trqfvmjtt4sij l32; /* params */
    gm_t0d02142k4tu8 l35; /* pat_ast */
    gm_trqfvmjtt4sij l36; /* args */
    gm_t0d02142k4tu8 l39; /* p */
    gm_trqfvmjtt4sij l40; /* binds */
    gm_trqfvmjtt4sij l43; /* arms */
    gm_tk9n3vlila9qg l45; /* frame */
    gm_trqfvmjtt4sij l46; /* ids */
    gm_trqfvmjtt4sij l47; /* clauses */
    gm_trqfvmjtt4sij l48; /* handlers */
    void * ret;
};
struct gm_fr_hoist_args_po0rnjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hoist_iter_value_cs229ip {
    gm_frame h;
    gm_tj2dlstgqla1c l3; /* place */
    gm_tfbvnc90lci8r l7; /* b */
    gm_tfbvnc90lci8r l11; /* b */
    gm_tfbvnc90lci8r l14; /* b */
    gm_tfbvnc90lci8r l15; /* i */
    gm_tfbvnc90lci8r l17; /* v */
    gm_tfbvnc90lci8r l19; /* o */
    gm_tfbvnc90lci8r l20; /* p */
    gm_tfbvnc90lci8r l21; /* expr */
    gm_trqfvmjtt4sij l26; /* pre */
    gm_tfbvnc90lci8r ret;
};
struct gm_fr_hoist_iterator_call_5rmgsce {
    gm_frame h;
    gm_tfbvnc90lci8r l5; /* new_callee */
    gm_tfbvnc90lci8r l8; /* b */
    gm_trqfvmjtt4sij l9; /* out */
    gm_tpff59ptr3s92 l12; /* a */
    gm_tfbvnc90lci8r l13; /* e */
    gm_tfbvnc90lci8r l15; /* e */
    gm_tfbvnc90lci8r l16; /* e */
    gm_tq6gu4l68u3rv l17; /* kind */
    gm_tfbvnc90lci8r ret;
};
struct gm_fr_iter_stable_k9c87oo {
    gm_frame h;
    gm_tpff59ptr3s92 l13; /* a */
    gm_tfbvnc90lci8r l14; /* x */
    gm_tfbvnc90lci8r l15; /* x */
    uint8_t ret;
};
struct gm_fr_iterator_arg_r0if28f {
    gm_frame h;
    gm_tddtjfggdat0d l3; /* sig */
    gm_tfbvnc90lci8r l11; /* call */
    gm_tlivgedjs5ps3 l12; /* fsig */
    gm_tj2dlstgqla1c ret;
};
struct gm_fr_iterator_call_as_value_00i5mch {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_lift_temps_7c6gk6l {
    gm_frame h;
    void * l0; /* cx */
    void * l2; /* pre */
    gm_b0_1 ret;
};
struct gm_fr_local_c3kjspo {
    gm_frame h;
    void * ret;
};
struct gm_fr_not_an_iterator_au10b3l {
    gm_frame h;
    void * ret;
};
struct gm_fr_place_iter_pre_qn5c5b5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_pop_ruvva9t {
    gm_frame h;
    gm_t8enl5g7b22jb l2; /* x */
    gm_te5gbhu3us3k4 ret;
};
bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
bool gm_eqlv5i3f25jk(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_counting_loop_s4u2ugd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint32_t a3, gm_t88rcucg1b7uk a4, void * a5, void * a6);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_binds_rk1ncsj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_denotes_type_jgfet92(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_enter_fn_vo5kel1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
__attribute__((aligned(4))) void * gm_f_enumerate_loop_5f6m89f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_exit_fn_kj6vbu8(void *gm_out, void * a0);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_43vto3v(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_t9suqla(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void * gm_f_for_each_value_5qklbg7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_for_iterator_cc5qo7n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_forget_label_breaks_vl4mn72(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_mpnc7rc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t04dp7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_hoist_args_po0rnjr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_hoist_iter_value_cs229ip(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_hoist_iterator_call_5rmgsce(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_irrefutable_lpkmeii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_iter_stable_k9c87oo(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_iterator_arg_r0if28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_iterator_call_as_value_00i5mch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lift_temps_7c6gk6l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_6l26q4a(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_p0pjtrh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_local_c3kjspo(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_cnt7p5u(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_label_3207tcb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2, gm_t1qr0p6kln6tf a3);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_not_an_iterator_au10b3l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_pattern_5gn6kid(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, void * a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_place_iter_pre_qn5c5b5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint64_t a3, void * a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bu0mlkm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_f5fsh94(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_jd7smj6(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_remove_vrfdutr(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rfind_ai5s2js(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_truncate_ua7l21h(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_k0gn7sac5pt[40] __attribute__((aligned(8)));
extern uint8_t gm_k1umgjqrbkf[40] __attribute__((aligned(8)));
extern uint8_t gm_k8gk4puesad[40] __attribute__((aligned(8)));
extern uint8_t gm_k8upcoeidcs[40] __attribute__((aligned(8)));
extern uint8_t gm_kb8poqrtf7g[40] __attribute__((aligned(8)));
extern uint8_t gm_kbng43moaud[40] __attribute__((aligned(8)));
extern uint8_t gm_kl5b02juaoa[40] __attribute__((aligned(8)));
extern uint8_t gm_kmnip3ksasl[40] __attribute__((aligned(8)));
extern uint8_t gm_kq6mn01fhkq[40] __attribute__((aligned(8)));
extern uint8_t gm_kq6te48ps2f[40] __attribute__((aligned(8)));
extern uint8_t gm_krlms0mdefs[40] __attribute__((aligned(8)));
extern uint8_t gm_kukq3ep6ok2[40] __attribute__((aligned(8)));
extern const uint8_t gm_sa39b9bt4jpna[36];
extern const uint8_t gm_sb8airrqr74u5[73];
extern const uint8_t gm_sbr6raqk787l2[10];
extern const uint8_t gm_sc0gdocdik6sv[7];
extern const uint8_t gm_sfvnlkgl40moh[72];
extern const uint8_t gm_sigq4ahgd0l2a[64];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_so4k586t2v79t[64];
extern const char gm_srcoimgd0jo62fr[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ss7qhfi2s00na[11];
extern const gm_type gm_tyo86ks9qkvt;
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
__attribute__((aligned(4))) void * gm_f_enumerate_loop_5f6m89f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4) {
    struct gm_fr_enumerate_loop_5f6m89f F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    void * gm_l2; /* call */
    gm_b16_8 gm_l3; /* bound */
    gm_t88rcucg1b7uk gm_l4; /* elem */
    gm_b16_8 gm_l6; /* args */
    uint32_t gm_l7; /* fid */
    void * gm_l8; /* sig */
    void * gm_l9; /* tys */
    gm_t88rcucg1b7uk gm_l10; /* sty */
    gm_t88rcucg1b7uk gm_l11; /* x_ty */
    gm_t88rcucg1b7uk gm_l13; /* usize_t */
    uint32_t gm_l14; /* s */
    uint32_t gm_l15; /* i */
    gm_closure gm_l16; /* local */
    void * gm_l18; /* item */
    void * gm_l19; /* lp */
    GM_STACK_CHECK(&F, 2338u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l2;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)34ULL)) goto ie2;
        gm_tsth9qu1dv0g3 t5 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t3) + 8) + 8) + 0));
        F.l5 = t5;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 8) + 8) + 40));
        gm_l6 = t6;
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
            if (t9 != ((uint8_t)0ULL)) goto ie8;
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 8));
            gm_l7 = t10;
            {
                uint32_t t12 = gm_l7;
                uint64_t t13 = (uint64_t)t12;
                uint64_t t14 = (uintptr_t)t13;
                void * t15 = gm_l0;
                void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
                uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 344) + 0) + 8);
                if (t14 >= (uintptr_t)(t17)) gm_trap_index(t14, t17, gm_srcoimgd0jo62fr, 123, 13);
                void * t18 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 344) + 0)) + t14 * 136);
                gm_l8 = t18;
            }
            {
                uint8_t t20 = 0;
                uint8_t t21 = 0;
                void * t22 = gm_l8;
                uint8_t t23 = (!((GM_LD(uintptr_t, (((uint8_t *)t22) + 0) + 8) == 9) && memcmp(GM_LD(void *, (((uint8_t *)t22) + 0)), gm_sbr6raqk787l2, 9) == 0));
                if (!t23) {
                    void * t24 = gm_l8;
                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 32));
                    uint64_t t26 = (uintptr_t)t25;
                    void * t27 = gm_l0;
                    void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
                    void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
                    uint64_t t30 = GM_LD(uintptr_t, ((((uint8_t *)t29) + 200) + 0) + 8);
                    if (t26 >= (uintptr_t)(t30)) gm_trap_index(t26, t30, gm_srcoimgd0jo62fr, 124, 43);
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t29) + 200) + 0)) + t26 * 184) + 112));
                    uint8_t t32 = (uint8_t)!t31;
                    t21 = t32;
                } else t21 = 1;
                if (!t21) {
                    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t34 = (!(t33 == ((uint64_t)1ULL)));
                    t20 = t34;
                } else t20 = 1;
                if (!t20) goto ie19;
                {
                    uint8_t t36 = 0;
                    F.ret = ((void *)0);
                    goto gm_fret;
                }
                goto ix19;
ie19:;
ix19:;
            }
            {
                void * t37 = gm_l0;
                void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
                void * t39 = (void *)(((uint8_t *)t38) + 8);
                gm_l9 = t39;
            }
            {
                void * t40 = gm_l0;
                void * t41 = (void *)(((uint8_t *)t40) + 96);
                void * t42 = gm_l0;
                void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
                uint64_t t44 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srcoimgd0jo62fr, 126, 35);
                gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t44 * 224) + 0));
                gm_t88rcucg1b7uk t47 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t41, t43, t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t47;
            }
            {
                void * t49 = gm_l9;
                gm_t88rcucg1b7uk t50 = gm_l10;
                gm_t53at80pfdus3 t51;
                gm_f_kind_jcnt4hm((void *)&t51, t49, GM_CONV(gm_csguankko4lmhd, t50));
                gm_t53at80pfdus3 t52 = t51;
                uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t52) + 0));
                if (t53 != ((uint8_t)10ULL)) goto ie48;
                gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t52) + 8) + 4));
                gm_l11 = t54;
                {
                    void * t56 = gm_l1;
                    gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t56) + 104));
                    F.l12 = t57;
                }
                {
                    void * t58 = gm_l9;
                    gm_t88rcucg1b7uk t59 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t58);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t59;
                }
                {
                    void * t60 = gm_l0;
                    gm_t88rcucg1b7uk t61 = gm_l10;
                    gm_tl874t2atnlh3 t62 = F.l12;
                    uint32_t t63 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t60, t61, t62);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l14 = t63;
                }
                {
                    void * t64 = gm_l0;
                    gm_t88rcucg1b7uk t65 = gm_l13;
                    gm_tl874t2atnlh3 t66 = F.l12;
                    uint32_t t67 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t64, t65, t66);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t67;
                }
                {
                    gm_closure t68 = ((gm_closure){(void *)gm_f_local_c3kjspo, (void *)&F.h, cx});
                    gm_l16 = t68;
                }
                {
                    gm_f_list_vp1djv1_2((void *)&F.l17, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t69 = (void *)((uint8_t *)&F.l17);
                    gm_closure t70 = gm_l16;
                    uint32_t t71 = gm_l15;
                    gm_t88rcucg1b7uk t72 = gm_l13;
                    gm_closure t73 = t70;
                    gm_node *ccx74 = t73.cx;
                    gm_node cn75; { gm_node *f = gm_find(cx, 587034433); if (f) { cn75 = *f; cn75.next = ccx74; ccx74 = &cn75; } }
                    void * t76 = t73.code;
                    void * t77;
                    if (GM_PTR_TAGGED(t76)) {
                        t77 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t76))(t71, GM_CONV(gm_csguankko4lmhd, t72));
                    } else {
                        t77 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t76)(t73.env, ccx74, NULL, t71, t72);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t78 = gm_f_push_d943vgp_2(t69, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t77)));
                    gm_tftb1odc739t5 t79 = t78;
                    if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 1, gm_srcoimgd0jo62fr, 134, 5);
                    gm_b0_1 t80 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 1));
                }
                {
                    void * t81 = (void *)((uint8_t *)&F.l17);
                    gm_t88rcucg1b7uk t82 = gm_l11;
                    gm_tlcl4u558l6p5 t83;
                    memset((uint8_t *)&t83 + 17, 0, 7);
                    gm_closure t84 = gm_l16;
                    uint32_t t85 = gm_l14;
                    gm_t88rcucg1b7uk t86 = gm_l10;
                    gm_closure t87 = t84;
                    gm_node *ccx88 = t87.cx;
                    gm_node cn89; { gm_node *f = gm_find(cx, 587034433); if (f) { cn89 = *f; cn89.next = ccx88; ccx88 = &cn89; } }
                    void * t90 = t87.code;
                    void * t91;
                    if (GM_PTR_TAGGED(t90)) {
                        t91 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t90))(t85, GM_CONV(gm_csguankko4lmhd, t86));
                    } else {
                        t91 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t90)(t87.env, ccx88, NULL, t85, t86);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(void *, (uint8_t *)&t83 + 0, t91);
                    gm_closure t92 = gm_l16;
                    uint32_t t93 = gm_l15;
                    gm_t88rcucg1b7uk t94 = gm_l13;
                    gm_closure t95 = t92;
                    gm_node *ccx96 = t95.cx;
                    gm_node cn97; { gm_node *f = gm_find(cx, 587034433); if (f) { cn97 = *f; cn97.next = ccx96; ccx96 = &cn97; } }
                    void * t98 = t95.code;
                    void * t99;
                    if (GM_PTR_TAGGED(t98)) {
                        t99 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t98))(t93, GM_CONV(gm_csguankko4lmhd, t94));
                    } else {
                        t99 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t98)(t95.env, ccx96, NULL, t93, t94);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(void *, (uint8_t *)&t83 + 8, t99);
                    GM_ST(uint8_t, (uint8_t *)&t83 + 16, ((uint8_t)0));
                    gm_t5brlu7fal70j t100;
                    memset(&t100, 0, sizeof t100);
                    GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)20ULL));
                    GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t100 + 8), t83);
                    gm_tl874t2atnlh3 t101 = F.l12;
                    void * t102 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t82, &t100, t101);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t103 = gm_f_push_d943vgp_2(t81, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t102)));
                    gm_tftb1odc739t5 t104 = t103;
                    if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_srcoimgd0jo62fr, 135, 5);
                    gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
                }
                {
                    gm_t88rcucg1b7uk t106 = gm_l4;
                    gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                    gm_t5brlu7fal70j t108;
                    memset(&t108, 0, sizeof t108);
                    GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)35ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t108 + 8), t107);
                    gm_tl874t2atnlh3 t109 = F.l12;
                    void * t110 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t106, &t108, t109);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l18 = t110;
                }
                {
                    void * t111 = gm_l0;
                    void * t112 = gm_l1;
                    gm_t88rcucg1b7uk t113 = gm_l4;
                    uint32_t t114 = gm_l15;
                    gm_t88rcucg1b7uk t115 = gm_l13;
                    gm_t88rcucg1b7uk t116 = gm_l13;
                    gm_closure t117 = gm_l16;
                    uint32_t t118 = gm_l14;
                    gm_t88rcucg1b7uk t119 = gm_l10;
                    gm_closure t120 = t117;
                    gm_node *ccx121 = t120.cx;
                    gm_node cn122; { gm_node *f = gm_find(cx, 587034433); if (f) { cn122 = *f; cn122.next = ccx121; ccx121 = &cn122; } }
                    void * t123 = t120.code;
                    void * t124;
                    if (GM_PTR_TAGGED(t123)) {
                        t124 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t123))(t118, GM_CONV(gm_csguankko4lmhd, t119));
                    } else {
                        t124 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t123)(t120.env, ccx121, NULL, t118, t119);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5brlu7fal70j t125;
                    memset(&t125, 0, sizeof t125);
                    GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)22ULL));
                    GM_ST(void *, ((uint8_t *)&t125 + 8), t124);
                    gm_tl874t2atnlh3 t126 = F.l12;
                    void * t127 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t116, &t125, t126);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t128 = gm_l18;
                    void * t129 = gm_f_counting_loop_s4u2ugd((void *)NULL, cx, NULL, t111, t112, t113, t114, t115, t127, t128);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l19 = t129;
                }
                {
                    gm_f_list_ir0h3tc_2((void *)&F.l20, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t130 = (void *)((uint8_t *)&F.l20);
                    gm_b16_8 t131 = gm_l3;
                    gm_b16_8 t132 = t131;
                    gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&t132));
                    gm_tftb1odc739t5 t134 = gm_f_extend_nkql463(t130, t133);
                    gm_tftb1odc739t5 t135 = t134;
                    if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_srcoimgd0jo62fr, 139, 5);
                    gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
                }
                {
                    void * t137 = (void *)((uint8_t *)&F.l20);
                    uint32_t t138 = gm_l14;
                    uint64_t t139 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t139 >= (uintptr_t)(t140)) gm_trap_index(t139, t140, gm_srcoimgd0jo62fr, 140, 25);
                    void * t141 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t139 * 224);
                    gm_ttggq7ng4kp3p t142;
                    GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t142 + 0), t138);
                    GM_ST(void *, ((uint8_t *)&t142 + 8), t141);
                    gm_trcmmd12cefb1 t143;
                    GM_ST(uint8_t, (uint8_t *)&t143 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t143 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t143 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t143 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t143 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t143 + 8), t142);
                    gm_tftb1odc739t5 t144 = gm_f_push_d6c76ud_2(t137, &t143);
                    gm_tftb1odc739t5 t145 = t144;
                    if (((((uint8_t *)&t145))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t145) + 1, gm_srcoimgd0jo62fr, 140, 5);
                    gm_b0_1 t146 = GM_LD(gm_b0_1, (((uint8_t *)&t145) + 1));
                }
                {
                    void * t147 = (void *)((uint8_t *)&F.l20);
                    uint32_t t148 = gm_l15;
                    gm_t88rcucg1b7uk t149 = gm_l13;
                    gm_tbci5ospk5nnn t150;
                    gm_f_zero_s25l990((void *)&t150, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5brlu7fal70j t151;
                    memset(&t151, 0, sizeof t151);
                    GM_ST(uint8_t, ((uint8_t *)&t151 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t151 + 8), t150);
                    gm_tl874t2atnlh3 t152 = F.l12;
                    void * t153 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t149, &t151, t152);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttggq7ng4kp3p t154;
                    GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t154 + 0), t148);
                    GM_ST(void *, ((uint8_t *)&t154 + 8), t153);
                    gm_trcmmd12cefb1 t155;
                    GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t155 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t155 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t155 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t155 + 8), t154);
                    gm_tftb1odc739t5 t156 = gm_f_push_d6c76ud_2(t147, &t155);
                    gm_tftb1odc739t5 t157 = t156;
                    if (((((uint8_t *)&t157))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t157) + 1, gm_srcoimgd0jo62fr, 141, 5);
                    gm_b0_1 t158 = GM_LD(gm_b0_1, (((uint8_t *)&t157) + 1));
                }
                {
                    void * t159 = (void *)((uint8_t *)&F.l20);
                    void * t160 = gm_l19;
                    gm_trcmmd12cefb1 t161;
                    GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t161 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t161 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t161 + 0), ((uint8_t)4ULL));
                    GM_ST(void *, ((uint8_t *)&t161 + 8), t160);
                    gm_tftb1odc739t5 t162 = gm_f_push_d6c76ud_2(t159, &t161);
                    gm_tftb1odc739t5 t163 = t162;
                    if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 1, gm_srcoimgd0jo62fr, 142, 5);
                    gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t163) + 1));
                }
                {
                    void * t165 = gm_l0;
                    void * t166 = GM_LD(void *, (((uint8_t *)t165) + 0));
                    gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                    gm_tl874t2atnlh3 t168 = F.l12;
                    void * t169 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t166, t167, t168);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t170 = 0;
                    F.ret = t169;
                    goto gm_fret;
                }
                goto ix48;
ie48:;
                {
                    uint8_t t172 = 0;
                    F.ret = ((void *)0);
                    goto gm_fret;
                }
ix48:;
            }
            goto ix8;
ie8:;
            {
                uint8_t t174 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
ix8:;
        }
        goto ix2;
ie2:;
        {
            uint8_t t176 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t9suqla(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_t9suqla F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_lift_temps_7c6gk6l *)((&F.h)->up))->l0));
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_lift_temps_7c6gk6l *)((&F.h)->up))->l2));
        gm_b0_1 t5 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t2, t3, t4);
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
__attribute__((aligned(4))) void * gm_f_for_iterator_cc5qo7n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_for_iterator_cc5qo7n F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* tys */
    uint32_t gm_l4; /* yield_eff */
    uint64_t gm_l5; /* start */
    void * gm_l6; /* call */
    gm_b16_8 gm_l7; /* bound */
    void * gm_l8; /* b */
    void * gm_l9; /* t */
    gm_tnpifv20irnph gm_l10; /* effects */
    uint32_t gm_l12; /* id */
    void * gm_l13; /* c */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_tiilmfd16v3fc gm_l19; /* e */
    void * gm_l20; /* e */
    gm_tiilmfd16v3fc gm_l21; /* eref */
    gm_t88rcucg1b7uk gm_l22; /* elem */
    gm_t88rcucg1b7uk gm_l23; /* t */
    uint64_t gm_l24; /* i */
    void * gm_l25; /* e */
    gm_t88rcucg1b7uk gm_l26; /* result_ty */
    gm_th2lqbo77pvsc gm_l28; /* label_name */
    gm_t08olq97rscn3 gm_l29; /* l */
    uint32_t gm_l30; /* label */
    uint32_t gm_l31; /* hid */
    uint32_t gm_l33; /* fn_id */
    uint32_t gm_l34; /* xid */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    void * gm_l41; /* pat */
    void * gm_l42; /* body */
    void * gm_l44; /* m */
    void * gm_l49; /* w */
    GM_STACK_CHECK(&F, 5581u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 104));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_l3 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_t9805qtonbsag t9 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t8) + 1956));
        gm_t9805qtonbsag t10 = t9;
        uint32_t t11;
        if (((((uint8_t *)&t10))[4] != 0)) {
            uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&t10) + 0));
            t11 = t12;
        } else {
            {
                void * t14 = gm_l0;
                gm_tl874t2atnlh3 t15 = gm_l2;
                gm_b16_8 t16;
                GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sa39b9bt4jpna));
                GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(35));
                gm_b0_1 t17 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t14, t15, t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t18 = gm_l0;
            void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
            gm_tl874t2atnlh3 t20 = gm_l2;
            void * t21 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        gm_l4 = t11;
    }
    {
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uintptr_t, ((((uint8_t *)t23) + 192) + 0) + 8);
        gm_l5 = t24;
    }
    {
        void * t25 = gm_l0;
        void * t26 = gm_l1;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 56));
        gm_tkn42u7na3t81 t28;
        memset(&t28, 0, sizeof t28);
        void * t29 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t25, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t29;
    }
    {
        gm_b16_8 t30 = gm_f_empty_slice_vj3ckdj_2();
        gm_l7 = t30;
    }
    {
        void * t32 = gm_l6;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
        if (t33 != ((uint8_t)46ULL)) goto ie31;
        void * t34 = GM_LD(void *, ((((uint8_t *)t32) + 8) + 8));
        gm_l8 = t34;
        void * t35 = gm_l8;
        void * t36 = GM_LD(void *, (((uint8_t *)t35) + 16));
        void * t37 = t36;
        if (!(GM_LD(void *, ((uint8_t *)&t37)) != 0)) goto ie31;
        void * t38 = GM_LD(void *, ((uint8_t *)&t37));
        gm_l9 = t38;
        void * t39 = gm_l9;
        uint8_t t40;
        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t39) + 8) + 0));
        if (t43 != ((uint8_t)34ULL)) goto mn42;
        t40 = ((uint8_t)1);
        goto me41;
mn42:;
        t40 = ((uint8_t)0);
        goto me41;
mn44:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 17, 51);
me41:;
        if (!t40) goto ie31;
        {
            void * t46 = gm_l8;
            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t46) + 0));
            gm_l7 = t47;
        }
        {
            void * t48 = gm_l9;
            gm_l6 = t48;
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        gm_tnpifv20irnph t49 = gm_f_empty_effects_r0iv3pv();
        gm_l10 = t49;
    }
    {
        void * t50 = gm_l6;
        uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t50) + 8) + 0));
        if (t53 != ((uint8_t)34ULL)) goto mn52;
        gm_tsth9qu1dv0g3 t54 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t50) + 8) + 8) + 0));
        F.l11 = t54;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
        switch (t60) { case ((uint8_t)0ULL): goto ma56; case ((uint8_t)1ULL): goto ma58; case ((uint8_t)2ULL): goto ma57; default: goto ma58; }
ma56:;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
        if (t62 != ((uint8_t)0ULL)) goto mn61;
        uint32_t t63 = GM_LD(uint32_t, (((uint8_t *)&F.l11) + 8));
        gm_l12 = t63;
        {
            uint32_t t65 = gm_l12;
            uint64_t t66 = (uint64_t)t65;
            uint64_t t67 = (uintptr_t)t66;
            void * t68 = gm_l0;
            void * t69 = GM_LD(void *, (((uint8_t *)t68) + 0));
            uint64_t t70 = GM_LD(uintptr_t, ((((uint8_t *)t69) + 344) + 0) + 8);
            if (t67 >= (uintptr_t)(t70)) gm_trap_index(t67, t70, gm_srcoimgd0jo62fr, 24, 36);
            gm_tnpifv20irnph t71 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t69) + 344) + 0)) + t67 * 136) + 64));
            gm_l10 = t71;
        }
        goto me55;
mn61:;
ma57:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
        if (t73 != ((uint8_t)2ULL)) goto mn72;
        void * t74 = GM_LD(void *, (((uint8_t *)&F.l11) + 8));
        gm_l13 = t74;
        void * t75 = gm_l3;
        void * t76 = gm_l0;
        void * t77 = (void *)(((uint8_t *)t76) + 96);
        void * t78 = gm_l0;
        void * t79 = GM_LD(void *, (((uint8_t *)t78) + 0));
        void * t80 = gm_l13;
        gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t80) + 0));
        gm_t88rcucg1b7uk t82 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t77, t79, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t83;
        gm_f_kind_jcnt4hm((void *)&t83, t75, GM_CONV(gm_csguankko4lmhd, t82));
        gm_t53at80pfdus3 t84 = t83;
        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&t84) + 0));
        if (t89 != ((uint8_t)15ULL)) goto pa88;
        gm_tddtjfggdat0d t90 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t84) + 8));
        F.l14 = t90;
        goto po87;
pa88:;
        uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t84) + 0));
        if (t92 != ((uint8_t)16ULL)) goto pa91;
        gm_tddtjfggdat0d t93 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t84) + 8));
        F.l14 = t93;
        goto po87;
pa91:;
        goto mn86;
po87:;
        {
            gm_tnpifv20irnph t95 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l14) + 24));
            gm_l10 = t95;
        }
        goto me85;
mn86:;
        goto me85;
mn96:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 25, 29);
me85:;
        goto me55;
mn72:;
ma58:;
        void * t99 = gm_l0;
        void * t100 = gm_l1;
        void * t101 = gm_l6;
        void * t102 = gm_f_not_an_iterator_au10b3l((void *)NULL, cx, NULL, t99, t100, t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t103 = 0;
        F.ret = t102;
        goto gm_fret;
        goto me55;
mn98:;
ma59:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 23, 29);
me55:;
        goto me51;
mn52:;
        void * t105 = gm_l0;
        void * t106 = gm_l1;
        void * t107 = gm_l6;
        void * t108 = gm_f_not_an_iterator_au10b3l((void *)NULL, cx, NULL, t105, t106, t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t109 = 0;
        F.ret = t108;
        goto gm_fret;
        goto me51;
mn104:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 22, 5);
me51:;
    }
    {
        gm_f_list_084fcd6((void *)&F.l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 0));
            gm_l17 = t111;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t113 = gm_l18;
                uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                uint8_t t115 = (t113 < t114);
                if (!t115) goto lb112;
                {
                    uint64_t t118 = gm_l18;
                    uint64_t t119 = (uintptr_t)t118;
                    uint64_t t120 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    gm_tiilmfd16v3fc t123 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t119 * 24));
                    gm_l19 = t123;
                    {
                        uint32_t t126 = GM_LD(uint32_t, (((uint8_t *)&gm_l19) + 0));
                        uint32_t t127 = gm_l4;
                        uint8_t t128 = ((t126 == t127));
                        if (!t128) goto ie125;
                        {
                            void * t130 = (void *)((uint8_t *)&F.l16);
                            gm_tftb1odc739t5 t131 = gm_f_push_d1r4h19(t130, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l19)));
                            gm_tftb1odc739t5 t132 = t131;
                            if (((((uint8_t *)&t132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t132) + 1, gm_srcoimgd0jo62fr, 35, 33);
                            gm_b0_1 t133 = GM_LD(gm_b0_1, (((uint8_t *)&t132) + 1));
                        }
                        goto ix125;
ie125:;
ix125:;
                    }
                    goto me121;
mn122:;
                    gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 34, 5);
me121:;
                }
lc112:;
                {
                    uint64_t t135 = gm_l18;
                    uint64_t r136; bool o137 = __builtin_add_overflow(t135, ((uint64_t)1ULL), &r136);
                    uint64_t t138 = r136;
                    gm_l18 = t138;
                }
            }
lb112:;
        }
    }
    {
        uint64_t t140 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
        uint8_t t141 = ((t140 == ((uint64_t)0ULL)));
        if (!t141) goto ie139;
        {
            void * t144 = gm_l0;
            void * t145 = gm_l1;
            void * t146 = gm_l6;
            void * t147 = gm_f_for_each_value_5qklbg7((void *)NULL, cx, NULL, t144, t145, t146);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t148 = t147;
            if (!(GM_LD(void *, ((uint8_t *)&t148)) != 0)) goto ie143;
            void * t149 = GM_LD(void *, ((uint8_t *)&t148));
            gm_l20 = t149;
            {
                void * t151 = gm_l20;
                uint8_t t152 = 0;
                F.ret = t151;
                goto gm_fret;
            }
            goto ix143;
ie143:;
ix143:;
        }
        {
            void * t153 = gm_l0;
            void * t154 = gm_l1;
            void * t155 = GM_LD(void *, (((uint8_t *)t154) + 56));
            gm_tl874t2atnlh3 t156 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t155) + 120));
            gm_b16_8 t157;
            GM_ST(void *, (uint8_t *)&t157, (void *)(gm_sigq4ahgd0l2a));
            GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(63));
            gm_b0_1 t158 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t153, t156, t157);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t159 = gm_l0;
            void * t160 = GM_LD(void *, (((uint8_t *)t159) + 0));
            gm_tl874t2atnlh3 t161 = gm_l2;
            void * t162 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t160, t161);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t163 = 0;
            F.ret = t162;
            goto gm_fret;
        }
        goto ix139;
ie139:;
ix139:;
    }
    {
        uint64_t t165 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
        uint8_t t166 = (t165 > ((uint64_t)1ULL));
        if (!t166) goto ie164;
        {
            void * t168 = gm_l0;
            void * t169 = gm_l1;
            void * t170 = GM_LD(void *, (((uint8_t *)t169) + 56));
            gm_tl874t2atnlh3 t171 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t170) + 120));
            gm_b16_8 t172;
            GM_ST(void *, (uint8_t *)&t172, (void *)(gm_sfvnlkgl40moh));
            GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(71));
            gm_b0_1 t173 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t168, t171, t172);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t174 = gm_l0;
            void * t175 = GM_LD(void *, (((uint8_t *)t174) + 0));
            gm_tl874t2atnlh3 t176 = gm_l2;
            void * t177 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t175, t176);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t178 = 0;
            F.ret = t177;
            goto gm_fret;
        }
        goto ix164;
ie164:;
ix164:;
    }
    {
        uint64_t t179 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t180 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
        if (t179 >= (uintptr_t)(t180)) gm_trap_index(t179, t180, gm_srcoimgd0jo62fr, 47, 13);
        gm_tiilmfd16v3fc t181 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l16) + 0)) + t179 * 24));
        gm_l21 = t181;
    }
    {
        void * t182 = gm_l3;
        gm_t88rcucg1b7uk t183 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t182);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l22 = t183;
    }
    {
        uint64_t t185 = GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 8) + 8);
        uint8_t t186 = (t185 > ((uint64_t)0ULL));
        if (!t186) goto ie184;
        uint64_t t187 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t188 = GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 8) + 8);
        if (t187 >= (uintptr_t)(t188)) gm_trap_index(t187, t188, gm_srcoimgd0jo62fr, 49, 42);
        uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l21) + 8)) + t187 * 40) + 0));
        if (t189 != ((uint8_t)0ULL)) goto ie184;
        gm_t88rcucg1b7uk t190 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l21) + 8)) + t187 * 40) + 8));
        gm_l23 = t190;
        {
            gm_t88rcucg1b7uk t192 = gm_l23;
            gm_l22 = t192;
        }
        goto ix184;
ie184:;
ix184:;
    }
    {
        uint64_t t193 = gm_l5;
        gm_l24 = t193;
    }
    {
        for (;;) {
            uint64_t t195 = gm_l24;
            void * t196 = gm_l0;
            uint64_t t197 = GM_LD(uintptr_t, ((((uint8_t *)t196) + 192) + 0) + 8);
            uint8_t t198 = (t195 < t197);
            if (!t198) goto lb194;
            {
                uint64_t t202 = gm_l24;
                uint64_t t203 = (uintptr_t)t202;
                void * t204 = gm_l0;
                uint64_t t205 = GM_LD(uintptr_t, ((((uint8_t *)t204) + 192) + 0) + 8);
                if (t203 >= (uintptr_t)(t205)) gm_trap_index(t203, t205, gm_srcoimgd0jo62fr, 53, 27);
                void * t206 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t204) + 192) + 0)) + t203 * 40) + 0);
                void * t207 = (void *)((uint8_t *)&gm_l21);
                int32_t t208 = gm_f_cmp_ref_8ntrqll(t206, t207);
                uint8_t t209 = ((t208 == ((int32_t)0LL)));
                if (!t209) goto ie201;
                {
                    {
                        void * t212 = gm_l0;
                        void * t213 = (void *)(((uint8_t *)t212) + 192);
                        uint64_t t214 = gm_l24;
                        gm_tj44oivvogvtl t215;
                        gm_f_remove_vrfdutr((void *)&t215, t213, t214);
                    }
                }
                goto ix201;
ie201:;
                {
                    uint64_t t217 = gm_l24;
                    uint64_t r218; bool o219 = __builtin_add_overflow(t217, ((uint64_t)1ULL), &r218);
                    if (o219) gm_trap("integer overflow in `+`", gm_srcoimgd0jo62fr, 56, 13);
                    uint64_t t220 = r218;
                    gm_l24 = t220;
                }
ix201:;
            }
lc194:;
        }
lb194:;
    }
    {
        void * t222 = gm_l0;
        void * t223 = gm_l1;
        void * t224 = gm_l6;
        gm_b16_8 t225 = gm_l7;
        gm_t88rcucg1b7uk t226 = gm_l22;
        void * t227 = gm_f_enumerate_loop_5f6m89f((void *)NULL, cx, NULL, t222, t223, t224, t225, t226);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t228 = t227;
        if (!(GM_LD(void *, ((uint8_t *)&t228)) != 0)) goto ie221;
        void * t229 = GM_LD(void *, ((uint8_t *)&t228));
        gm_l25 = t229;
        {
            void * t231 = gm_l25;
            uint8_t t232 = 0;
            F.ret = t231;
            goto gm_fret;
        }
        goto ix221;
ie221:;
ix221:;
    }
    {
        void * t233 = gm_l6;
        gm_t88rcucg1b7uk t234 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t233) + 0));
        gm_l26 = t234;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l27, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t235 = (void *)((uint8_t *)&F.l27);
        gm_b16_8 t236 = gm_l7;
        gm_b16_8 t237 = t236;
        gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&t237));
        gm_tftb1odc739t5 t239 = gm_f_extend_nkql463(t235, t238);
        gm_tftb1odc739t5 t240 = t239;
        if (((((uint8_t *)&t240))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t240) + 1, gm_srcoimgd0jo62fr, 63, 5);
        gm_b0_1 t241 = GM_LD(gm_b0_1, (((uint8_t *)&t240) + 1));
    }
    {
        void * t242 = gm_l0;
        void * t243 = gm_l6;
        void * t244 = (void *)((uint8_t *)&F.l27);
        gm_b0_1 t245 = gm_f_hoist_args_po0rnjr((void *)NULL, cx, NULL, t242, t243, t244);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t246 = gm_l0;
        gm_b0_1 t247 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t246);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_th2lqbo77pvsc t248;
        memset(&t248, 0, sizeof t248);
        gm_l28 = t248;
    }
    {
        void * t250 = gm_l1;
        if (!(((((uint8_t *)t250) + 0))[32] != 0)) goto ie249;
        gm_t08olq97rscn3 t251 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)t250) + 0));
        gm_l29 = t251;
        {
            void * t253 = gm_l0;
            gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 0));
            gm_t930q5l38hdap t255;
            memset(&t255, 0, sizeof t255);
            GM_ST(uint8_t, ((uint8_t *)&t255 + 0), ((uint8_t)2ULL));
            gm_tl874t2atnlh3 t256 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l29) + 16));
            gm_b0_1 t257 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t253, t254, &t255, t256);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 0));
            gm_th2lqbo77pvsc t259;
            GM_ST(uint8_t, (uint8_t *)&t259 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t259 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t259 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t259), t258);
            ((uint8_t *)&t259)[16] = 1;
            gm_l28 = t259;
        }
        goto ix249;
ie249:;
ix249:;
    }
    {
        void * t260 = gm_l0;
        gm_t88rcucg1b7uk t261 = gm_l26;
        gm_t1qr0p6kln6tf t262;
        GM_ST(uint8_t, (uint8_t *)&t262 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t262 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t262 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t262 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t262 + 4), t261);
        uint32_t t263 = gm_f_new_label_3207tcb((void *)NULL, cx, NULL, t260, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l28)), ((uint8_t)1), t262);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l30 = t263;
    }
    {
        void * t264 = gm_l0;
        void * t265 = GM_LD(void *, (((uint8_t *)t264) + 0));
        uint64_t t266 = GM_LD(uintptr_t, ((((uint8_t *)t265) + 504) + 0) + 8);
        uint32_t t267 = (uint32_t)t266;
        gm_l31 = t267;
    }
    {
        void * t268 = gm_l0;
        void * t269 = GM_LD(void *, (((uint8_t *)t268) + 0));
        void * t270 = (void *)(((uint8_t *)t269) + 504);
        gm_t1cs2fjnrqap1 t271;
        memcpy((uint8_t *)&t271 + 0, ((uint8_t *)&gm_l21), 24);
        gm_b16_8 t272 = gm_f_empty_slice_d914i5u();
        GM_ST(gm_b16_8, (uint8_t *)&t271 + 24, t272);
        gm_tftb1odc739t5 t273 = gm_f_push_bu0mlkm(t270, &t271);
        gm_tftb1odc739t5 t274 = t273;
        if (((((uint8_t *)&t274))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t274) + 1, gm_srcoimgd0jo62fr, 73, 5);
        gm_b0_1 t275 = GM_LD(gm_b0_1, (((uint8_t *)&t274) + 1));
    }
    {
        gm_f_list_af74s0q((void *)&F.l32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t276 = (void *)((uint8_t *)&F.l32);
        gm_t5v0c8ia6a5mo t277;
        memset((uint8_t *)&t277 + 49, 0, 7);
        gm_b16_8 t278;
        GM_ST(void *, (uint8_t *)&t278, (void *)(gm_sc0gdocdik6sv));
        GM_ST(uintptr_t, (uint8_t *)&t278 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&t277 + 0, t278);
        gm_t88rcucg1b7uk t279 = gm_l22;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t277 + 16, t279);
        gm_tl874t2atnlh3 t280 = gm_l2;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t277 + 20, t280);
        GM_ST(void *, (uint8_t *)&t277 + 32, ((void *)0));
        gm_t9805qtonbsag t281;
        memset(&t281, 0, sizeof t281);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t277 + 40, t281);
        GM_ST(uint8_t, (uint8_t *)&t277 + 48, ((uint8_t)0));
        gm_tftb1odc739t5 t282 = gm_f_push_o8u01sh(t276, &t277);
        gm_tftb1odc739t5 t283 = t282;
        if (((((uint8_t *)&t283))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t283) + 1, gm_srcoimgd0jo62fr, 76, 5);
        gm_b0_1 t284 = GM_LD(gm_b0_1, (((uint8_t *)&t283) + 1));
    }
    {
        void * t285 = gm_l0;
        void * t286 = GM_LD(void *, (((uint8_t *)t285) + 0));
        gm_t9qkh0rsooc8d t287;
        GM_ST(uint8_t, ((uint8_t *)&t287 + 0), ((uint8_t)2ULL));
        gm_b16_8 t288;
        GM_ST(void *, (uint8_t *)&t288, (void *)(gm_ss7qhfi2s00na));
        GM_ST(uintptr_t, (uint8_t *)&t288 + 8, (uintptr_t)(10));
        gm_tl874t2atnlh3 t289 = gm_l2;
        void * t290 = gm_l0;
        uint64_t t291 = GM_LD(uint64_t, (((uint8_t *)t290) + 8));
        gm_b16_8 t292 = GM_LD(gm_b16_8, (((uint8_t *)&F.l32) + 0));
        void * t293 = gm_l3;
        gm_t88rcucg1b7uk t294 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t293);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t295 = gm_f_empty_effects_r0iv3pv();
        uint32_t t296 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t286, t287, t288, t289, t291, t292, t294, t295);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l33 = t296;
    }
    {
        void * t297 = gm_l0;
        uint32_t t298 = gm_l33;
        void * t299 = gm_l3;
        gm_t88rcucg1b7uk t300 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t299);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t301 = gm_f_empty_effects_r0iv3pv();
        gm_b0_1 t302 = gm_f_enter_fn_vo5kel1((void *)NULL, cx, NULL, t297, t298, t300, t301);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t303 = gm_l0;
        gm_tnqmlaad6uchl t304;
        memset((uint8_t *)&t304 + 1, 0, 3);
        memset((uint8_t *)&t304 + 28, 0, 4);
        gm_t9qkh0rsooc8d t305;
        GM_ST(uint8_t, ((uint8_t *)&t305 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t304 + 0, t305);
        uint32_t t306 = gm_l31;
        gm_t9805qtonbsag t307;
        GM_ST(uint8_t, (uint8_t *)&t307 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t307 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t307), t306);
        ((uint8_t *)&t307)[4] = 1;
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t304 + 4, t307);
        gm_t9805qtonbsag t308;
        memset(&t308, 0, sizeof t308);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t304 + 12, t308);
        gm_tkn42u7na3t81 t309;
        memset(&t309, 0, sizeof t309);
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t304 + 20, t309);
        gm_tuanp0jbrlb6j t310;
        memset((uint8_t *)&t310 + 28, 0, 4);
        memcpy((uint8_t *)&t310 + 0, ((uint8_t *)&gm_l28), 24);
        uint32_t t311 = gm_l30;
        GM_ST(uint32_t, (uint8_t *)&t310 + 24, t311);
        gm_tjvtgq0p0sehv t312;
        GM_ST(uint8_t, (uint8_t *)&t312 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t312 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t312 + 36, 0);
        GM_ST(gm_tuanp0jbrlb6j, ((uint8_t *)&t312), t310);
        ((uint8_t *)&t312)[32] = 1;
        GM_ST(gm_tjvtgq0p0sehv, (uint8_t *)&t304 + 32, t312);
        gm_t88rcucg1b7uk t313 = gm_l26;
        gm_tkn42u7na3t81 t314;
        GM_ST(uint8_t, (uint8_t *)&t314 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t314 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t314), t313);
        ((uint8_t *)&t314)[4] = 1;
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t304 + 72, t314);
        gm_tiilmfd16v3fc t315 = gm_l21;
        gm_tqu3m2l3n8mnc t316;
        GM_ST(uint8_t, (uint8_t *)&t316 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t316 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t316 + 28, 0);
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t316), t315);
        ((uint8_t *)&t316)[24] = 1;
        GM_ST(gm_tqu3m2l3n8mnc, (uint8_t *)&t304 + 80, t316);
        gm_t403eokj7ifg9 t317;
        GM_ST(uint8_t, (uint8_t *)&t317 + 113, 0);
        GM_ST(uint16_t, (uint8_t *)&t317 + 114, 0);
        GM_ST(uint32_t, (uint8_t *)&t317 + 116, 0);
        GM_ST(gm_tnqmlaad6uchl, ((uint8_t *)&t317), t304);
        ((uint8_t *)&t317)[112] = 1;
        GM_ST(gm_t403eokj7ifg9, (((uint8_t *)t303) + 800), t317);
    }
    {
        void * t318 = gm_l0;
        gm_b16_8 t319;
        GM_ST(void *, (uint8_t *)&t319, (void *)(gm_sc0gdocdik6sv));
        GM_ST(uintptr_t, (uint8_t *)&t319 + 8, (uintptr_t)(6));
        gm_t88rcucg1b7uk t320 = gm_l22;
        gm_tl874t2atnlh3 t321 = gm_l2;
        uint32_t t322 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t318, t319, t320, ((uint8_t)0), t321);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l34 = t322;
    }
    {
        uint64_t t323 = (uintptr_t)((uint64_t)0ULL);
        void * t324 = gm_l1;
        uint64_t t325 = GM_LD(uintptr_t, (((uint8_t *)t324) + 40) + 8);
        if (t323 >= (uintptr_t)(t325)) gm_trap_index(t323, t325, gm_srcoimgd0jo62fr, 82, 16);
        gm_t0d02142k4tu8 t326 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t324) + 40)) + t323 * 64));
        F.l35 = t326;
    }
    {
        void * t328 = gm_l1;
        uint64_t t329 = GM_LD(uintptr_t, (((uint8_t *)t328) + 40) + 8);
        uint8_t t330 = (!(t329 == ((uint64_t)1ULL)));
        if (!t330) goto ie327;
        {
            gm_f_list_ttksl9g((void *)&F.l36, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t333 = gm_l1;
                gm_b16_8 t334 = GM_LD(gm_b16_8, (((uint8_t *)t333) + 40));
                gm_l37 = t334;
            }
            {
                gm_l38 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t336 = gm_l38;
                    uint64_t t337 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                    uint8_t t338 = (t336 < t337);
                    if (!t338) goto lb335;
                    {
                        uint64_t t341 = gm_l38;
                        uint64_t t342 = (uintptr_t)t341;
                        uint64_t t343 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                        gm_t0d02142k4tu8 t346 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l37)) + t342 * 64));
                        F.l39 = t346;
                        {
                            void * t348 = (void *)((uint8_t *)&F.l36);
                            gm_tnrgict8sthcv t349;
                            memset((uint8_t *)&t349 + 116, 0, 4);
                            gm_t1s75i08qtptd t350;
                            memset(&t350, 0, sizeof t350);
                            GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t349 + 0, t350);
                            memcpy((uint8_t *)&t349 + 40, ((uint8_t *)&F.l39), 64);
                            gm_tl874t2atnlh3 t351 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l39) + 48));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t349 + 104, t351);
                            gm_tftb1odc739t5 t352 = gm_f_push_l74t9rq(t348, &t349);
                            gm_tftb1odc739t5 t353 = t352;
                            if (((((uint8_t *)&t353))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t353) + 1, gm_srcoimgd0jo62fr, 85, 27);
                            gm_b0_1 t354 = GM_LD(gm_b0_1, (((uint8_t *)&t353) + 1));
                        }
                        goto me344;
mn345:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 85, 9);
me344:;
                    }
lc335:;
                    {
                        uint64_t t356 = gm_l38;
                        uint64_t r357; bool o358 = __builtin_add_overflow(t356, ((uint64_t)1ULL), &r357);
                        uint64_t t359 = r357;
                        gm_l38 = t359;
                    }
                }
lb335:;
            }
        }
        {
            memset((uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l35)) + 4, 0, 4);
            memset((uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l35)) + 60, 0, 4);
            uint64_t t360 = (uintptr_t)((uint64_t)0ULL);
            void * t361 = gm_l1;
            uint64_t t362 = GM_LD(uintptr_t, (((uint8_t *)t361) + 40) + 8);
            if (t360 >= (uintptr_t)(t362)) gm_trap_index(t360, t362, gm_srcoimgd0jo62fr, 86, 35);
            uint32_t t363 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t361) + 40)) + t360 * 64) + 0));
            gm_b16_8 t364 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 0));
            gm_to361lgknsu69 t365;
            GM_ST(uint8_t, (uint8_t *)&t365 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t365 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t365 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t365 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t365 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t365 + 0), ((uint8_t)8ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t365 + 8), t364);
            uint64_t t366 = (uintptr_t)((uint64_t)0ULL);
            void * t367 = gm_l1;
            uint64_t t368 = GM_LD(uintptr_t, (((uint8_t *)t367) + 40) + 8);
            if (t366 >= (uintptr_t)(t368)) gm_trap_index(t366, t368, gm_srcoimgd0jo62fr, 86, 81);
            gm_tl874t2atnlh3 t369 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)t367) + 40)) + t366 * 64) + 48));
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l35)) + 0, t363);
            GM_ST(gm_to361lgknsu69, (uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l35)) + 8, t365);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l35)) + 48, t369);
        }
        goto ix327;
ie327:;
ix327:;
    }
    {
        gm_f_list_6l26q4a((void *)&F.l40, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t370 = gm_l0;
        void * t371 = (void *)((uint8_t *)&F.l35);
        gm_t88rcucg1b7uk t372 = gm_l22;
        gm_t9qkh0rsooc8d t373;
        GM_ST(uint8_t, ((uint8_t *)&t373 + 0), ((uint8_t)1ULL));
        void * t374 = (void *)((uint8_t *)&F.l40);
        void * t375 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t370, t371, t372, t373, t374);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l41 = t375;
    }
    {
        void * t377 = gm_l3;
        gm_t88rcucg1b7uk t378 = gm_l22;
        uint8_t t379 = gm_f_is_error_8k27gd1(t377, GM_CONV(gm_csguankko4lmhd, t378));
        uint8_t t380 = (uint8_t)!t379;
        if (!t380) goto ie376;
        void * t381 = gm_l0;
        void * t382 = GM_LD(void *, (((uint8_t *)t381) + 0));
        void * t383 = gm_l41;
        uint8_t t384 = gm_f_irrefutable_lpkmeii((void *)NULL, cx, NULL, t382, t383);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t385 = (uint8_t)!t384;
        if (!t385) goto ie376;
        {
            void * t387 = gm_l0;
            gm_tl874t2atnlh3 t388 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l35) + 48));
            gm_b16_8 t389;
            GM_ST(void *, (uint8_t *)&t389, (void *)(gm_so4k586t2v79t));
            GM_ST(uintptr_t, (uint8_t *)&t389 + 8, (uintptr_t)(63));
            gm_b0_1 t390 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t387, t388, t389);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix376;
ie376:;
ix376:;
    }
    {
        void * t391 = gm_l0;
        void * t392 = (void *)((uint8_t *)&F.l40);
        gm_b0_1 t393 = gm_f_declare_binds_rk1ncsj((void *)NULL, cx, NULL, t391, t392);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t394 = gm_l0;
        void * t395 = gm_l1;
        void * t396 = (void *)(((uint8_t *)t395) + 64);
        void * t397 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t394, t396, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l42 = t397;
    }
    {
        gm_f_list_58nr42v_3((void *)&F.l43, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t398 = (void *)((uint8_t *)&F.l43);
        gm_tbbqefuhv2r8h t399;
        void * t400 = gm_l41;
        GM_ST(void *, (uint8_t *)&t399 + 0, t400);
        GM_ST(void *, (uint8_t *)&t399 + 8, ((void *)0));
        void * t401 = gm_l42;
        GM_ST(void *, (uint8_t *)&t399 + 16, t401);
        gm_tftb1odc739t5 t402 = gm_f_push_bpovr2k_3(t398, &t399);
        gm_tftb1odc739t5 t403 = t402;
        if (((((uint8_t *)&t403))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t403) + 1, gm_srcoimgd0jo62fr, 96, 5);
        gm_b0_1 t404 = GM_LD(gm_b0_1, (((uint8_t *)&t403) + 1));
    }
    {
        void * t405 = gm_l3;
        gm_t88rcucg1b7uk t406 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t405);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkaapcii86udk t407;
        gm_t88rcucg1b7uk t408 = gm_l22;
        uint32_t t409 = gm_l34;
        gm_t5brlu7fal70j t410;
        memset(&t410, 0, sizeof t410);
        GM_ST(uint8_t, ((uint8_t *)&t410 + 0), ((uint8_t)15ULL));
        GM_ST(uint32_t, ((uint8_t *)&t410 + 8), t409);
        gm_tl874t2atnlh3 t411 = gm_l2;
        void * t412 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t408, &t410, t411);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t407 + 0, t412);
        gm_b16_8 t413 = GM_LD(gm_b16_8, (((uint8_t *)&F.l43) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t407 + 8, t413);
        gm_t5brlu7fal70j t414;
        memset(&t414, 0, sizeof t414);
        GM_ST(uint8_t, ((uint8_t *)&t414 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t414 + 8), t407);
        gm_tl874t2atnlh3 t415 = gm_l2;
        void * t416 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t406, &t414, t415);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l44 = t416;
    }
    {
        void * t417 = gm_l0;
        gm_f_exit_fn_kj6vbu8((void *)&F.l45, t417);
    }
    {
        void * t418 = gm_l0;
        void * t419 = (void *)(((uint8_t *)t418) + 192);
        gm_b16_8 t420 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 16));
        gm_b16_8 t421 = t420;
        gm_b16_8 t422 = GM_LD(gm_b16_8, ((uint8_t *)&t421));
        gm_tftb1odc739t5 t423 = gm_f_extend_43vto3v(t419, t422);
        gm_tftb1odc739t5 t424 = t423;
        if (((((uint8_t *)&t424))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t424) + 1, gm_srcoimgd0jo62fr, 99, 5);
        gm_b0_1 t425 = GM_LD(gm_b0_1, (((uint8_t *)&t424) + 1));
    }
    {
        uint32_t t426 = gm_l33;
        uint64_t t427 = (uint64_t)t426;
        uint64_t t428 = (uintptr_t)t427;
        void * t429 = gm_l0;
        void * t430 = GM_LD(void *, (((uint8_t *)t429) + 0));
        uint64_t t431 = GM_LD(uintptr_t, ((((uint8_t *)t430) + 384) + 0) + 8);
        if (t428 >= (uintptr_t)(t431)) gm_trap_index(t428, t431, gm_srcoimgd0jo62fr, 100, 5);
        void * t432 = gm_l0;
        gm_t9805qtonbsag t433 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t432) + 400));
        GM_ST(gm_t9805qtonbsag, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t430) + 384) + 0)) + t428 * 64) + 48), t433);
    }
    {
        gm_f_list_go2qg0b((void *)&F.l46, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t434 = (void *)((uint8_t *)&F.l46);
        uint32_t t435 = gm_l34;
        gm_tftb1odc739t5 t436 = gm_f_push_im9d4ro(t434, t435);
        gm_tftb1odc739t5 t437 = t436;
        if (((((uint8_t *)&t437))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t437) + 1, gm_srcoimgd0jo62fr, 102, 5);
        gm_b0_1 t438 = GM_LD(gm_b0_1, (((uint8_t *)&t437) + 1));
    }
    {
        void * t439 = gm_l0;
        void * t440 = (void *)(((uint8_t *)t439) + 704);
        gm_tl93vjpcesj48 t441;
        memset((uint8_t *)&t441 + 4, 0, 4);
        uint32_t t442 = gm_l33;
        GM_ST(uint32_t, (uint8_t *)&t441 + 0, t442);
        gm_b16_8 t443 = GM_LD(gm_b16_8, (((uint8_t *)&F.l46) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t441 + 8, t443);
        gm_b16_8 t444 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t441 + 24, t444);
        void * t445 = gm_l44;
        GM_ST(void *, (uint8_t *)&t441 + 40, t445);
        gm_tftb1odc739t5 t446 = gm_f_push_jd7smj6(t440, &t441);
        gm_tftb1odc739t5 t447 = t446;
        if (((((uint8_t *)&t447))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t447) + 1, gm_srcoimgd0jo62fr, 103, 5);
        gm_b0_1 t448 = GM_LD(gm_b0_1, (((uint8_t *)&t447) + 1));
    }
    {
        {
            void * t450 = gm_l0;
            void * t451 = (void *)(((uint8_t *)t450) + 232);
            gm_te5gbhu3us3k4 t452;
            gm_f_pop_ruvva9t((void *)&t452, t451);
        }
    }
    {
        {
            void * t454 = gm_l0;
            uint32_t t455 = gm_l30;
            uint8_t t456 = gm_f_forget_label_breaks_vl4mn72(t454, t455);
        }
    }
    {
        void * t457 = gm_l0;
        gm_f_pop_scope_g5j7o4e(t457);
    }
    {
        gm_f_list_go2qg0b((void *)&F.l47, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t458 = (void *)((uint8_t *)&F.l47);
        uint32_t t459 = gm_l33;
        gm_tftb1odc739t5 t460 = gm_f_push_im9d4ro(t458, t459);
        gm_tftb1odc739t5 t461 = t460;
        if (((((uint8_t *)&t461))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t461) + 1, gm_srcoimgd0jo62fr, 108, 5);
        gm_b0_1 t462 = GM_LD(gm_b0_1, (((uint8_t *)&t461) + 1));
    }
    {
        uint32_t t463 = gm_l31;
        uint64_t t464 = (uint64_t)t463;
        uint64_t t465 = (uintptr_t)t464;
        void * t466 = gm_l0;
        void * t467 = GM_LD(void *, (((uint8_t *)t466) + 0));
        uint64_t t468 = GM_LD(uintptr_t, ((((uint8_t *)t467) + 504) + 0) + 8);
        if (t465 >= (uintptr_t)(t468)) gm_trap_index(t465, t468, gm_srcoimgd0jo62fr, 109, 5);
        gm_b16_8 t469 = GM_LD(gm_b16_8, (((uint8_t *)&F.l47) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t467) + 504) + 0)) + t465 * 40) + 24), t469);
    }
    {
        gm_f_list_p0pjtrh((void *)&F.l48, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t470 = (void *)((uint8_t *)&F.l48);
        void * t471 = gm_l3;
        uint32_t t472 = gm_l31;
        gm_t53at80pfdus3 t473;
        memset(&t473, 0, sizeof t473);
        GM_ST(uint8_t, ((uint8_t *)&t473 + 0), ((uint8_t)25ULL));
        GM_ST(uint32_t, ((uint8_t *)&t473 + 8), t472);
        gm_t88rcucg1b7uk t474 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t471, &t473);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t475 = gm_l31;
        gm_t5brlu7fal70j t476;
        memset(&t476, 0, sizeof t476);
        GM_ST(uint8_t, ((uint8_t *)&t476 + 0), ((uint8_t)28ULL));
        GM_ST(uint32_t, ((uint8_t *)&t476 + 8), t475);
        gm_tl874t2atnlh3 t477 = gm_l2;
        void * t478 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t474, &t476, t477);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4lnf71mruqpt t479;
        GM_ST(uint8_t, (uint8_t *)&t479 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t479 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t479 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t479 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t479 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t479 + 0), ((uint8_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t479 + 8), t478);
        gm_tftb1odc739t5 t480 = gm_f_push_f5fsh94(t470, &t479);
        gm_tftb1odc739t5 t481 = t480;
        if (((((uint8_t *)&t481))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t481) + 1, gm_srcoimgd0jo62fr, 111, 5);
        gm_b0_1 t482 = GM_LD(gm_b0_1, (((uint8_t *)&t481) + 1));
    }
    {
        gm_t88rcucg1b7uk t483 = gm_l26;
        gm_t710lsrq1m8gk t484;
        memset((uint8_t *)&t484 + 4, 0, 4);
        uint32_t t485 = gm_l30;
        GM_ST(uint32_t, (uint8_t *)&t484 + 0, t485);
        gm_b16_8 t486 = GM_LD(gm_b16_8, (((uint8_t *)&F.l48) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t484 + 8, t486);
        GM_ST(void *, (uint8_t *)&t484 + 24, ((void *)0));
        void * t487 = gm_l6;
        GM_ST(void *, (uint8_t *)&t484 + 32, t487);
        gm_t5brlu7fal70j t488;
        memset(&t488, 0, sizeof t488);
        GM_ST(uint8_t, ((uint8_t *)&t488 + 0), ((uint8_t)58ULL));
        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t488 + 8), t484);
        gm_tl874t2atnlh3 t489 = gm_l2;
        void * t490 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t483, &t488, t489);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l49 = t490;
    }
    {
        uint64_t t492 = GM_LD(uintptr_t, (((uint8_t *)&F.l27) + 0) + 8);
        uint8_t t493 = ((t492 == ((uint64_t)0ULL)));
        if (!t493) goto ie491;
        {
            void * t495 = gm_l49;
            uint8_t t496 = 0;
            F.ret = t495;
            goto gm_fret;
        }
        goto ix491;
ie491:;
ix491:;
    }
    {
        gm_t88rcucg1b7uk t497 = gm_l26;
        gm_b16_8 t498 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
        void * t499 = gm_l49;
        gm_tl874t2atnlh3 t500 = gm_l2;
        void * t501 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t497, t498, t499, t500);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t502 = 0;
        F.ret = t501;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_hoist_args_po0rnjr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_hoist_args_po0rnjr F;
    void * gm_l0; /* cx */
    void * gm_l1; /* call */
    void * gm_l2; /* pre */
    gm_b16_8 gm_l3; /* args */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    void * gm_l7; /* a */
    uint32_t gm_l8; /* t */
    void * gm_l9; /* v */
    GM_STACK_CHECK(&F, 892u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)34ULL)) goto ie2;
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 8) + 8) + 40));
        gm_l3 = t5;
        {
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l4 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = gm_l4;
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    {
                        uint64_t t15 = gm_l5;
                        uint64_t t16 = t15;
                        uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t16));
                        gm_l6 = t19;
                        {
                            uint64_t t21 = gm_l6;
                            uint64_t t22 = (uintptr_t)t21;
                            uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_srcoimgd0jo62fr, 159, 15);
                            void * t24 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t22 * 224);
                            gm_l7 = t24;
                        }
                        {
                            void * t26 = gm_l7;
                            uint8_t t27;
                            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                            switch (t34) { case ((uint8_t)0ULL): goto ma29; case ((uint8_t)1ULL): goto ma32; case ((uint8_t)2ULL): goto ma32; case ((uint8_t)3ULL): goto ma32; case ((uint8_t)4ULL): goto ma31; case ((uint8_t)5ULL): goto ma30; case ((uint8_t)6ULL): goto ma32; case ((uint8_t)7ULL): goto ma32; case ((uint8_t)8ULL): goto ma32; case ((uint8_t)9ULL): goto ma32; case ((uint8_t)10ULL): goto ma32; case ((uint8_t)11ULL): goto ma32; case ((uint8_t)12ULL): goto ma32; case ((uint8_t)13ULL): goto ma32; case ((uint8_t)14ULL): goto ma32; case ((uint8_t)15ULL): goto ma32; case ((uint8_t)16ULL): goto ma32; case ((uint8_t)17ULL): goto ma32; case ((uint8_t)18ULL): goto ma32; case ((uint8_t)19ULL): goto ma32; case ((uint8_t)20ULL): goto ma32; case ((uint8_t)21ULL): goto ma32; case ((uint8_t)22ULL): goto ma32; case ((uint8_t)23ULL): goto ma32; case ((uint8_t)24ULL): goto ma32; case ((uint8_t)25ULL): goto ma32; case ((uint8_t)26ULL): goto ma32; case ((uint8_t)27ULL): goto ma32; case ((uint8_t)28ULL): goto ma32; case ((uint8_t)29ULL): goto ma32; case ((uint8_t)30ULL): goto ma32; case ((uint8_t)31ULL): goto ma32; case ((uint8_t)32ULL): goto ma32; case ((uint8_t)33ULL): goto ma32; case ((uint8_t)34ULL): goto ma32; case ((uint8_t)35ULL): goto ma32; case ((uint8_t)36ULL): goto ma32; case ((uint8_t)37ULL): goto ma32; case ((uint8_t)38ULL): goto ma32; case ((uint8_t)39ULL): goto ma32; case ((uint8_t)40ULL): goto ma32; case ((uint8_t)41ULL): goto ma32; case ((uint8_t)42ULL): goto ma32; case ((uint8_t)43ULL): goto ma32; case ((uint8_t)44ULL): goto ma32; case ((uint8_t)45ULL): goto ma32; case ((uint8_t)46ULL): goto ma32; case ((uint8_t)47ULL): goto ma32; case ((uint8_t)48ULL): goto ma32; case ((uint8_t)49ULL): goto ma32; case ((uint8_t)50ULL): goto ma32; case ((uint8_t)51ULL): goto ma32; case ((uint8_t)52ULL): goto ma32; case ((uint8_t)53ULL): goto ma32; case ((uint8_t)54ULL): goto ma32; case ((uint8_t)55ULL): goto ma32; case ((uint8_t)56ULL): goto ma32; case ((uint8_t)57ULL): goto ma32; case ((uint8_t)58ULL): goto ma32; default: goto ma32; }
ma29:;
                            uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                            if (t36 != ((uint8_t)0ULL)) goto mn35;
                            t27 = ((uint8_t)1);
                            goto me28;
mn35:;
ma30:;
                            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                            if (t38 != ((uint8_t)5ULL)) goto mn37;
                            t27 = ((uint8_t)1);
                            goto me28;
mn37:;
ma31:;
                            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                            if (t40 != ((uint8_t)4ULL)) goto mn39;
                            t27 = ((uint8_t)1);
                            goto me28;
mn39:;
ma32:;
                            t27 = ((uint8_t)0);
                            goto me28;
mn41:;
ma33:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 160, 12);
me28:;
                            if (!t27) goto ie25;
                            {
                                goto lc9;
                            }
                            goto ix25;
ie25:;
ix25:;
                        }
                        {
                            void * t43 = gm_l0;
                            void * t44 = gm_l7;
                            void * t45 = gm_l2;
                            gm_b0_1 t46 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t43, t44, t45);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                        }
                        {
                            void * t47 = gm_l0;
                            void * t48 = gm_l7;
                            gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t48) + 0));
                            void * t50 = gm_l7;
                            gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t50) + 208));
                            uint32_t t52 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t47, t49, t51);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                            gm_l8 = t52;
                        }
                        {
                            void * t53 = gm_l7;
                            gm_tq7s98ihonkju t54 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t53)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k1umgjqrbkf)));
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                            gm_tq7s98ihonkju t55 = t54;
                            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_srcoimgd0jo62fr, 166, 14);
                            void * t56 = GM_LD(void *, (((uint8_t *)&t55) + 8));
                            gm_l9 = t56;
                        }
                        {
                            void * t57 = gm_l7;
                            uint8_t *t58 = ((uint8_t *)t57);
                            void * t59 = gm_l7;
                            gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t59) + 0));
                            uint32_t t61 = gm_l8;
                            gm_t5brlu7fal70j t62;
                            memset(&t62, 0, sizeof t62);
                            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)15ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t62 + 8), t61);
                            void * t63 = gm_l7;
                            gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 208));
                            void * t65 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t60, &t62, t64);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                            gm_tfpf0s89fpb3r t66 = GM_LD(gm_tfpf0s89fpb3r, ((uint8_t *)t65));
                            GM_ST(gm_tfpf0s89fpb3r, t58, t66);
                        }
                        {
                            void * t67 = gm_l2;
                            uint32_t t68 = gm_l8;
                            void * t69 = gm_l9;
                            gm_ttggq7ng4kp3p t70;
                            GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t70 + 0), t68);
                            GM_ST(void *, ((uint8_t *)&t70 + 8), t69);
                            gm_trcmmd12cefb1 t71;
                            GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t71 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t71 + 8), t70);
                            gm_tftb1odc739t5 t72 = gm_f_push_d6c76ud_2(t67, &t71);
                            gm_tftb1odc739t5 t73 = t72;
                            if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_srcoimgd0jo62fr, 168, 9);
                            gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
                        }
                        goto me17;
mn18:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 158, 5);
me17:;
                    }
lc9:;
                    {
                        uint64_t t76 = gm_l5;
                        uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
                        uint64_t t79 = r77;
                        gm_l5 = t79;
                    }
                }
gm_ll13:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb9; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc9; }
                }
                goto gm_lad;
lb9:;
            }
        }
        goto ix2;
ie2:;
        {
            gm_b0_1 t81;
            uint8_t t82 = 0;
            F.ret = t81;
            goto gm_fret;
        }
ix2:;
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
__attribute__((aligned(4))) void gm_f_hoist_iter_value_cs229ip(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_hoist_iter_value_cs229ip F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    uint8_t gm_l2; /* receiver */
    void * gm_l4; /* x */
    void * gm_l5; /* base */
    gm_t08olq97rscn3 gm_l6; /* name */
    void * gm_l8; /* base */
    uint32_t gm_l9; /* index */
    gm_tl874t2atnlh3 gm_l10; /* index_span */
    void * gm_l12; /* base */
    void * gm_l13; /* index */
    void * gm_l16; /* x */
    void * gm_l18; /* operand */
    uint64_t gm_l22; /* mark */
    void * gm_l23; /* v */
    gm_t88rcucg1b7uk gm_l24; /* ty */
    uint8_t gm_l25; /* keep_place */
    gm_b16_8 gm_l27; /* name */
    uint32_t gm_l28; /* local */
    GM_STACK_CHECK(&F, 5200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l1;
        uint8_t t5 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t5) goto ie2;
        {
            void * t7 = gm_l1;
            memmove(gm_out, ((uint8_t *)t7), 136);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_tj2dlstgqla1c t8;
        memset(&t8, 0, sizeof t8);
        F.l3 = t8;
    }
    {
        void * t9 = gm_l1;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        switch (t20) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma18; case ((uint8_t)2ULL): goto ma18; case ((uint8_t)3ULL): goto ma18; case ((uint8_t)4ULL): goto ma18; case ((uint8_t)5ULL): goto ma18; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma18; case ((uint8_t)8ULL): goto ma18; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma18; case ((uint8_t)11ULL): goto ma18; case ((uint8_t)12ULL): goto ma17; case ((uint8_t)13ULL): goto ma18; case ((uint8_t)14ULL): goto ma18; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma18; case ((uint8_t)17ULL): goto ma18; case ((uint8_t)18ULL): goto ma16; case ((uint8_t)19ULL): goto ma18; case ((uint8_t)20ULL): goto ma15; case ((uint8_t)21ULL): goto ma18; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma18; case ((uint8_t)25ULL): goto ma18; case ((uint8_t)26ULL): goto ma18; case ((uint8_t)27ULL): goto ma18; case ((uint8_t)28ULL): goto ma18; case ((uint8_t)29ULL): goto ma18; case ((uint8_t)30ULL): goto ma18; case ((uint8_t)31ULL): goto ma18; case ((uint8_t)32ULL): goto ma18; case ((uint8_t)33ULL): goto ma18; case ((uint8_t)34ULL): goto ma18; case ((uint8_t)35ULL): goto ma18; case ((uint8_t)36ULL): goto ma18; case ((uint8_t)37ULL): goto ma18; case ((uint8_t)38ULL): goto ma18; case ((uint8_t)39ULL): goto ma18; case ((uint8_t)40ULL): goto ma18; case ((uint8_t)41ULL): goto ma18; case ((uint8_t)42ULL): goto ma18; case ((uint8_t)43ULL): goto ma18; case ((uint8_t)44ULL): goto ma18; case ((uint8_t)45ULL): goto ma18; case ((uint8_t)46ULL): goto ma18; case ((uint8_t)47ULL): goto ma18; case ((uint8_t)48ULL): goto ma18; case ((uint8_t)49ULL): goto ma18; case ((uint8_t)50ULL): goto ma18; default: goto ma18; }
ma11:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t22 != ((uint8_t)9ULL)) goto mn21;
        void * t23 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
        gm_l4 = t23;
        void * t24 = gm_l0;
        void * t25 = gm_l4;
        uint8_t t26 = gm_l2;
        gm_f_hoist_iter_value_cs229ip((void *)&(*(gm_tfbvnc90lci8r *)gm_out), (void *)NULL, cx, NULL, t24, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me10;
mn21:;
ma12:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t28 != ((uint8_t)6ULL)) goto mn27;
        {
            void * t30 = gm_l1;
            gm_tfbvnc90lci8r t31 = GM_LD(gm_tfbvnc90lci8r, ((uint8_t *)t30));
            gm_tj2dlstgqla1c t32;
            GM_ST(uint8_t, (uint8_t *)&t32 + 137, 0);
            GM_ST(uint16_t, (uint8_t *)&t32 + 138, 0);
            GM_ST(uint32_t, (uint8_t *)&t32 + 140, 0);
            GM_ST(gm_tfbvnc90lci8r, ((uint8_t *)&t32), t31);
            ((uint8_t *)&t32)[136] = 1;
            F.l3 = t32;
        }
        goto me10;
mn27:;
ma13:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t34 != ((uint8_t)22ULL)) goto mn33;
        void * t35 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 0));
        gm_l5 = t35;
        gm_t08olq97rscn3 t36 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t9) + 8) + 8) + 8));
        gm_l6 = t36;
        {
            void * t38 = gm_l0;
            void * t39 = gm_l5;
            gm_f_hoist_iter_value_cs229ip((void *)&F.l7, (void *)NULL, cx, NULL, t38, t39, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tfbvnc90lci8r t40;
            memset((uint8_t *)&t40 + 4, 0, 4);
            memset((uint8_t *)&t40 + 132, 0, 4);
            GM_ST(uint32_t, (uint8_t *)&t40 + 0, ((uint32_t)4294967295ULL));
            gm_tioie2ju3q4ul t41;
            gm_tq7s98ihonkju t42 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l7)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kq6te48ps2f)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t43 = t42;
            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcoimgd0jo62fr, 314, 78);
            void * t44 = GM_LD(void *, (((uint8_t *)&t43) + 8));
            GM_ST(void *, (uint8_t *)&t41 + 0, t44);
            memcpy((uint8_t *)&t41 + 8, ((uint8_t *)&gm_l6), 32);
            gm_t6e9d80inoppc t45;
            memset(&t45, 0, sizeof t45);
            GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)22ULL));
            GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t45 + 8), t41);
            GM_ST(gm_t6e9d80inoppc, (uint8_t *)&t40 + 8, t45);
            void * t46 = gm_l1;
            gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t46) + 120));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t40 + 120, t47);
            gm_tj2dlstgqla1c t48;
            GM_ST(uint8_t, (uint8_t *)&t48 + 137, 0);
            GM_ST(uint16_t, (uint8_t *)&t48 + 138, 0);
            GM_ST(uint32_t, (uint8_t *)&t48 + 140, 0);
            GM_ST(gm_tfbvnc90lci8r, ((uint8_t *)&t48), t40);
            ((uint8_t *)&t48)[136] = 1;
            F.l3 = t48;
        }
        goto me10;
mn33:;
ma14:;
        uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t50 != ((uint8_t)23ULL)) goto mn49;
        void * t51 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 0));
        gm_l8 = t51;
        uint32_t t52 = GM_LD(uint32_t, (((((uint8_t *)t9) + 8) + 8) + 8));
        gm_l9 = t52;
        gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t9) + 8) + 8) + 12));
        gm_l10 = t53;
        {
            void * t55 = gm_l0;
            void * t56 = gm_l8;
            gm_f_hoist_iter_value_cs229ip((void *)&F.l11, (void *)NULL, cx, NULL, t55, t56, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tfbvnc90lci8r t57;
            memset((uint8_t *)&t57 + 4, 0, 4);
            memset((uint8_t *)&t57 + 132, 0, 4);
            GM_ST(uint32_t, (uint8_t *)&t57 + 0, ((uint32_t)4294967295ULL));
            gm_tupk62cr6lbdu t58;
            gm_tq7s98ihonkju t59 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l11)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kukq3ep6ok2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t60 = t59;
            if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 8, gm_srcoimgd0jo62fr, 318, 83);
            void * t61 = GM_LD(void *, (((uint8_t *)&t60) + 8));
            GM_ST(void *, (uint8_t *)&t58 + 0, t61);
            uint32_t t62 = gm_l9;
            GM_ST(uint32_t, (uint8_t *)&t58 + 8, t62);
            gm_tl874t2atnlh3 t63 = gm_l10;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t58 + 12, t63);
            gm_t6e9d80inoppc t64;
            memset(&t64, 0, sizeof t64);
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)23ULL));
            GM_ST(gm_tupk62cr6lbdu, ((uint8_t *)&t64 + 8), t58);
            GM_ST(gm_t6e9d80inoppc, (uint8_t *)&t57 + 8, t64);
            void * t65 = gm_l1;
            gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 120));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t57 + 120, t66);
            gm_tj2dlstgqla1c t67;
            GM_ST(uint8_t, (uint8_t *)&t67 + 137, 0);
            GM_ST(uint16_t, (uint8_t *)&t67 + 138, 0);
            GM_ST(uint32_t, (uint8_t *)&t67 + 140, 0);
            GM_ST(gm_tfbvnc90lci8r, ((uint8_t *)&t67), t57);
            ((uint8_t *)&t67)[136] = 1;
            F.l3 = t67;
        }
        goto me10;
mn49:;
ma15:;
        uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t69 != ((uint8_t)20ULL)) goto mn68;
        void * t70 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 0));
        gm_l12 = t70;
        void * t71 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 8));
        gm_l13 = t71;
        {
            void * t73 = gm_l0;
            void * t74 = gm_l12;
            gm_f_hoist_iter_value_cs229ip((void *)&F.l14, (void *)NULL, cx, NULL, t73, t74, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t75 = gm_l0;
            void * t76 = gm_l13;
            gm_f_hoist_iter_value_cs229ip((void *)&F.l15, (void *)NULL, cx, NULL, t75, t76, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tfbvnc90lci8r t77;
            memset((uint8_t *)&t77 + 4, 0, 4);
            memset((uint8_t *)&t77 + 132, 0, 4);
            GM_ST(uint32_t, (uint8_t *)&t77 + 0, ((uint32_t)4294967295ULL));
            gm_tq7s98ihonkju t78 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l14)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8gk4puesad)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t79 = t78;
            if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 8, gm_srcoimgd0jo62fr, 323, 78);
            void * t80 = GM_LD(void *, (((uint8_t *)&t79) + 8));
            gm_tq7s98ihonkju t81 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l15)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kbng43moaud)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t82 = t81;
            if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 8, gm_srcoimgd0jo62fr, 323, 94);
            void * t83 = GM_LD(void *, (((uint8_t *)&t82) + 8));
            gm_t3mf58gv9kf3n t84;
            GM_ST(void *, ((uint8_t *)&t84 + 0), t80);
            GM_ST(void *, ((uint8_t *)&t84 + 8), t83);
            gm_t6e9d80inoppc t85;
            memset(&t85, 0, sizeof t85);
            GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)20ULL));
            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t85 + 8), t84);
            GM_ST(gm_t6e9d80inoppc, (uint8_t *)&t77 + 8, t85);
            void * t86 = gm_l1;
            gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t86) + 120));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t77 + 120, t87);
            gm_tj2dlstgqla1c t88;
            GM_ST(uint8_t, (uint8_t *)&t88 + 137, 0);
            GM_ST(uint16_t, (uint8_t *)&t88 + 138, 0);
            GM_ST(uint32_t, (uint8_t *)&t88 + 140, 0);
            GM_ST(gm_tfbvnc90lci8r, ((uint8_t *)&t88), t77);
            ((uint8_t *)&t88)[136] = 1;
            F.l3 = t88;
        }
        goto me10;
mn68:;
ma16:;
        uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t90 != ((uint8_t)18ULL)) goto mn89;
        void * t91 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
        gm_l16 = t91;
        {
            void * t93 = gm_l0;
            void * t94 = gm_l16;
            gm_f_hoist_iter_value_cs229ip((void *)&F.l17, (void *)NULL, cx, NULL, t93, t94, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tfbvnc90lci8r t95;
            memset((uint8_t *)&t95 + 4, 0, 4);
            memset((uint8_t *)&t95 + 132, 0, 4);
            GM_ST(uint32_t, (uint8_t *)&t95 + 0, ((uint32_t)4294967295ULL));
            gm_tq7s98ihonkju t96 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l17)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krlms0mdefs)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t97 = t96;
            if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 8, gm_srcoimgd0jo62fr, 327, 72);
            void * t98 = GM_LD(void *, (((uint8_t *)&t97) + 8));
            gm_t6e9d80inoppc t99;
            memset(&t99, 0, sizeof t99);
            GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)18ULL));
            GM_ST(void *, ((uint8_t *)&t99 + 8), t98);
            GM_ST(gm_t6e9d80inoppc, (uint8_t *)&t95 + 8, t99);
            void * t100 = gm_l1;
            gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t100) + 120));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t95 + 120, t101);
            gm_tj2dlstgqla1c t102;
            GM_ST(uint8_t, (uint8_t *)&t102 + 137, 0);
            GM_ST(uint16_t, (uint8_t *)&t102 + 138, 0);
            GM_ST(uint32_t, (uint8_t *)&t102 + 140, 0);
            GM_ST(gm_tfbvnc90lci8r, ((uint8_t *)&t102), t95);
            ((uint8_t *)&t102)[136] = 1;
            F.l3 = t102;
        }
        goto me10;
mn89:;
ma17:;
        uint8_t t104 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t104 != ((uint8_t)12ULL)) goto mn103;
        uint8_t t105 = GM_LD(uint8_t, ((((((uint8_t *)t9) + 8) + 8) + 0) + 0));
        if (t105 != ((uint8_t)2ULL)) goto mn103;
        void * t106 = GM_LD(void *, (((((uint8_t *)t9) + 8) + 8) + 8));
        gm_l18 = t106;
        {
            void * t108 = gm_l0;
            void * t109 = gm_l18;
            gm_f_hoist_iter_value_cs229ip((void *)&F.l19, (void *)NULL, cx, NULL, t108, t109, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            memset((uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 4, 0, 4);
            memset((uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 132, 0, 4);
            gm_t9qkh0rsooc8d t110;
            GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)2ULL));
            gm_tq7s98ihonkju t111 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l19)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kb8poqrtf7g)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t112 = t111;
            if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 8, gm_srcoimgd0jo62fr, 331, 93);
            void * t113 = GM_LD(void *, (((uint8_t *)&t112) + 8));
            gm_t8ghgl2jj0qha t114;
            GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t114 + 0), t110);
            GM_ST(void *, ((uint8_t *)&t114 + 8), t113);
            gm_t6e9d80inoppc t115;
            memset(&t115, 0, sizeof t115);
            GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)12ULL));
            GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t115 + 8), t114);
            void * t116 = gm_l1;
            gm_tl874t2atnlh3 t117 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t116) + 120));
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 0, ((uint32_t)4294967295ULL));
            GM_ST(gm_t6e9d80inoppc, (uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 8, t115);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 120, t117);
            goto gm_fdone;
        }
        goto me10;
mn103:;
ma18:;
        goto me10;
mn118:;
ma19:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 309, 5);
me10:;
    }
    {
        gm_tfbvnc90lci8r t120;
        if (!((((uint8_t *)&F.l3))[136] != 0)) goto ie121;
        gm_tfbvnc90lci8r t122 = GM_LD(gm_tfbvnc90lci8r, ((uint8_t *)&F.l3));
        F.l20 = t122;
        gm_tfbvnc90lci8r t124 = F.l20;
        gm_tfbvnc90lci8r t125 = t124;
        t120 = t125;
        goto ix121;
ie121:;
        void * t127 = gm_l1;
        gm_tfbvnc90lci8r t128 = GM_LD(gm_tfbvnc90lci8r, ((uint8_t *)t127));
        gm_tfbvnc90lci8r t129 = t128;
        t120 = t129;
ix121:;
        F.l21 = t120;
    }
    {
        void * t130 = gm_l0;
        uint64_t t131 = GM_LD(uintptr_t, ((((uint8_t *)t130) + 192) + 0) + 8);
        gm_l22 = t131;
    }
    {
        void * t132 = gm_l0;
        void * t133 = (void *)((uint8_t *)&F.l21);
        gm_tkn42u7na3t81 t134;
        memset(&t134, 0, sizeof t134);
        void * t135 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t132, t133, t134);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l23 = t135;
    }
    {
        uint8_t t137;
        uint8_t t140 = GM_LD(uint8_t, ((((uint8_t *)&F.l21) + 8) + 0));
        if (t140 != ((uint8_t)19ULL)) goto mn139;
        t137 = ((uint8_t)1);
        goto me138;
mn139:;
        t137 = ((uint8_t)0);
        goto me138;
mn141:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 340, 8);
me138:;
        if (!t137) goto ie136;
        gm_tl874t2atnlh3 t142 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l21) + 120));
        void * t143 = gm_l0;
        gm_b16_8 t144 = GM_LD(gm_b16_8, ((((uint8_t *)t143) + 960) + 0));
        gm_b16_8 t145 = t144;
        uint8_t t146 = 0;
        gm_tl874t2atnlh3 t147 = t142;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t145) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t145))) + i * 12, ((uint8_t *)&t147))) { t146 = 1; break; }
        uint8_t t148 = (uint8_t)(t146);
        if (!t148) goto ie136;
        {
            void * t150 = gm_l0;
            void * t151 = (void *)(((uint8_t *)t150) + 192);
            uint64_t t152 = gm_l22;
            gm_f_truncate_ua7l21h(t151, t152);
        }
        {
            void * t153 = gm_l0;
            void * t154 = (void *)((uint8_t *)&F.l21);
            gm_f_hoist_iterator_call_5rmgsce((void *)&(*(gm_tfbvnc90lci8r *)gm_out), (void *)NULL, cx, NULL, t153, t154);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix136;
ie136:;
ix136:;
    }
    {
        void * t155 = gm_l0;
        void * t156 = (void *)(((uint8_t *)t155) + 96);
        void * t157 = gm_l0;
        void * t158 = GM_LD(void *, (((uint8_t *)t157) + 0));
        void * t159 = gm_l23;
        gm_t88rcucg1b7uk t160 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t159) + 0));
        gm_t88rcucg1b7uk t161 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t156, t158, t160);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l24 = t161;
    }
    {
        uint8_t t162 = 0;
        uint8_t t163;
        if (((((uint8_t *)&F.l3))[136] != 0)) goto mn165;
        t163 = ((uint8_t)0);
        goto me164;
mn165:;
        t163 = ((uint8_t)1);
        goto me164;
mn166:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 345, 19);
me164:;
        if (t163) {
            uint8_t t167 = 0;
            uint8_t t168 = gm_l2;
            if (!t168) {
                void * t169 = gm_l0;
                void * t170 = GM_LD(void *, (((uint8_t *)t169) + 0));
                void * t171 = (void *)(((uint8_t *)t170) + 8);
                gm_t88rcucg1b7uk t172 = gm_l24;
                gm_t53at80pfdus3 t173;
                gm_f_kind_jcnt4hm((void *)&t173, t171, GM_CONV(gm_csguankko4lmhd, t172));
                gm_t53at80pfdus3 t174 = t173;
                uint8_t t175;
                uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&t174) + 0));
                if (t178 != ((uint8_t)11ULL)) goto mn177;
                t175 = ((uint8_t)1);
                goto me176;
mn177:;
                t175 = ((uint8_t)0);
                goto me176;
mn179:;
                gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 345, 54);
me176:;
                t167 = t175;
            } else t167 = 1;
            t162 = t167;
        } else t162 = 0;
        gm_l25 = t162;
    }
    {
        uint8_t t181 = 0;
        uint8_t t182 = 0;
        uint8_t t183 = 0;
        void * t184 = gm_l0;
        void * t185 = GM_LD(void *, (((uint8_t *)t184) + 0));
        void * t186 = (void *)(((uint8_t *)t185) + 8);
        gm_t88rcucg1b7uk t187 = gm_l24;
        uint8_t t188 = gm_f_is_error_8k27gd1(t186, GM_CONV(gm_csguankko4lmhd, t187));
        if (!t188) {
            void * t189 = gm_l0;
            void * t190 = GM_LD(void *, (((uint8_t *)t189) + 0));
            void * t191 = (void *)(((uint8_t *)t190) + 8);
            gm_t88rcucg1b7uk t192 = gm_l24;
            uint8_t t193 = gm_f_has_infer_3o82a0b(t191, GM_CONV(gm_csguankko4lmhd, t192));
            t183 = t193;
        } else t183 = 1;
        if (!t183) {
            void * t194 = gm_l0;
            void * t195 = GM_LD(void *, (((uint8_t *)t194) + 0));
            void * t196 = (void *)(((uint8_t *)t195) + 8);
            gm_t88rcucg1b7uk t197 = gm_l24;
            gm_t53at80pfdus3 t198;
            gm_f_kind_jcnt4hm((void *)&t198, t196, GM_CONV(gm_csguankko4lmhd, t197));
            gm_t53at80pfdus3 t199 = t198;
            uint8_t t200;
            uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&t199) + 0));
            if (t203 != ((uint8_t)15ULL)) goto mn202;
            t200 = ((uint8_t)1);
            goto me201;
mn202:;
            t200 = ((uint8_t)0);
            goto me201;
mn204:;
            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 348, 59);
me201:;
            t182 = t200;
        } else t182 = 1;
        if (!t182) {
            uint8_t t205 = gm_l25;
            t181 = t205;
        } else t181 = 1;
        if (!t181) goto ie180;
        {
            memmove(gm_out, ((uint8_t *)&F.l21), 136);
            goto gm_fdone;
        }
        goto ix180;
ie180:;
ix180:;
    }
    {
        void * t207 = gm_l0;
        gm_trqfvmjtt4sij t208 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t207) + 1160));
        F.l26 = t208;
    }
    {
        void * t209 = gm_l0;
        uint8_t *t210 = (((uint8_t *)t209) + 1160);
        gm_f_list_ir0h3tc_2((void *)&(*(gm_trqfvmjtt4sij *)t210), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t211 = gm_l0;
        void * t212 = gm_l23;
        void * t213 = (void *)((uint8_t *)&F.l26);
        gm_b0_1 t214 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t211, t212, t213);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t215 = gm_l0;
        uint64_t t216 = GM_LD(uintptr_t, ((((uint8_t *)t215) + 16) + 0) + 8);
        gm_t4ce0jd3tdro4 t217;
        GM_ST(uint64_t, ((uint8_t *)&t217 + 0), t216);
        gm_tabb710d9mq2e t218;
        gm_f_format_t04dp7b((void *)&t218, (void *)NULL, cx, NULL, t217);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t219 = t218;
        if (((((uint8_t *)&t219))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t219) + 8, gm_srcoimgd0jo62fr, 352, 20);
        gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&t219) + 8));
        gm_b16_8 t221 = t220;
        gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&t221));
        gm_l27 = t222;
    }
    {
        void * t223 = gm_l0;
        gm_b16_8 t224 = gm_l27;
        gm_t88rcucg1b7uk t225 = gm_l24;
        void * t226 = gm_l1;
        gm_tl874t2atnlh3 t227 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t226) + 120));
        uint32_t t228 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t223, t224, t225, ((uint8_t)1), t227);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l28 = t228;
    }
    {
        void * t229 = (void *)((uint8_t *)&F.l26);
        uint32_t t230 = gm_l28;
        void * t231 = gm_l23;
        gm_ttggq7ng4kp3p t232;
        GM_ST(uint32_t, (uint8_t *)&t232 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t232 + 0), t230);
        GM_ST(void *, ((uint8_t *)&t232 + 8), t231);
        gm_trcmmd12cefb1 t233;
        GM_ST(uint8_t, (uint8_t *)&t233 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t233 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t233 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t233 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t233 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t233 + 8), t232);
        gm_tftb1odc739t5 t234 = gm_f_push_d6c76ud_2(t229, &t233);
        gm_tftb1odc739t5 t235 = t234;
        if (((((uint8_t *)&t235))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t235) + 1, gm_srcoimgd0jo62fr, 354, 5);
        gm_b0_1 t236 = GM_LD(gm_b0_1, (((uint8_t *)&t235) + 1));
    }
    {
        void * t237 = gm_l0;
        gm_trqfvmjtt4sij t238 = F.l26;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t237) + 1160), t238);
    }
    {
        void * t239 = gm_l0;
        gm_b16_8 t240 = gm_l27;
        uint32_t t241 = gm_l28;
        gm_t930q5l38hdap t242;
        memset(&t242, 0, sizeof t242);
        GM_ST(uint8_t, ((uint8_t *)&t242 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t242 + 8), t241);
        void * t243 = gm_l1;
        gm_tl874t2atnlh3 t244 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t243) + 120));
        gm_b0_1 t245 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t239, t240, &t242, t244);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memset((uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 4, 0, 4);
        memset((uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 132, 0, 4);
        gm_t08olq97rscn3 t246;
        memset((uint8_t *)&t246 + 28, 0, 4);
        gm_b16_8 t247 = gm_l27;
        GM_ST(gm_b16_8, (uint8_t *)&t246 + 0, t247);
        void * t248 = gm_l1;
        gm_tl874t2atnlh3 t249 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t248) + 120));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t246 + 16, t249);
        gm_t6e9d80inoppc t250;
        memset(&t250, 0, sizeof t250);
        GM_ST(uint8_t, ((uint8_t *)&t250 + 0), ((uint8_t)6ULL));
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t250 + 8), t246);
        void * t251 = gm_l1;
        gm_tl874t2atnlh3 t252 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t251) + 120));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 0, ((uint32_t)4294967295ULL));
        GM_ST(gm_t6e9d80inoppc, (uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 8, t250);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 120, t252);
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
__attribute__((aligned(4))) void gm_f_hoist_iterator_call_5rmgsce(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_hoist_iterator_call_5rmgsce F;
    void * gm_l0; /* cx */
    void * gm_l1; /* call */
    void * gm_l2; /* callee */
    gm_b16_8 gm_l3; /* args */
    uint8_t gm_l4; /* trailing_comma */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t08olq97rscn3 gm_l14; /* n */
    GM_STACK_CHECK(&F, 2964u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)19ULL)) goto ie2;
        void * t5 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 0));
        gm_l2 = t5;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 8) + 8) + 8));
        gm_l3 = t6;
        uint8_t t7 = GM_LD(uint8_t, (((((uint8_t *)t3) + 8) + 8) + 24));
        gm_l4 = t7;
        {
            void * t9 = gm_l2;
            gm_tfbvnc90lci8r t10 = GM_LD(gm_tfbvnc90lci8r, ((uint8_t *)t9));
            F.l5 = t10;
        }
        {
            void * t12 = gm_l2;
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            if (t13 != ((uint8_t)22ULL)) goto ie11;
            void * t14 = GM_LD(void *, (((((uint8_t *)t12) + 8) + 8) + 0));
            gm_l6 = t14;
            gm_t08olq97rscn3 t15 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t12) + 8) + 8) + 8));
            gm_l7 = t15;
            {
                void * t18 = gm_l0;
                void * t19 = gm_l6;
                uint8_t t20 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t18, t19);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t21 = (uint8_t)!t20;
                if (!t21) goto ie17;
                {
                    void * t23 = gm_l0;
                    void * t24 = gm_l6;
                    gm_f_hoist_iter_value_cs229ip((void *)&F.l8, (void *)NULL, cx, NULL, t23, t24, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    memset((uint8_t *)&(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l5)) + 4, 0, 4);
                    memset((uint8_t *)&(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l5)) + 132, 0, 4);
                    void * t25 = gm_l2;
                    uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)t25) + 0));
                    gm_tioie2ju3q4ul t27;
                    gm_tq7s98ihonkju t28 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l8)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kl5b02juaoa)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tq7s98ihonkju t29 = t28;
                    if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_srcoimgd0jo62fr, 261, 69);
                    void * t30 = GM_LD(void *, (((uint8_t *)&t29) + 8));
                    GM_ST(void *, (uint8_t *)&t27 + 0, t30);
                    memcpy((uint8_t *)&t27 + 8, ((uint8_t *)&gm_l7), 32);
                    gm_t6e9d80inoppc t31;
                    memset(&t31, 0, sizeof t31);
                    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)22ULL));
                    GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t31 + 8), t27);
                    void * t32 = gm_l2;
                    gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 120));
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l5)) + 0, t26);
                    GM_ST(gm_t6e9d80inoppc, (uint8_t *)&(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l5)) + 8, t31);
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l5)) + 120, t33);
                }
                goto ix17;
ie17:;
ix17:;
            }
            goto ix11;
ie11:;
ix11:;
        }
        {
            gm_f_list_6oos54v((void *)&F.l9, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t35 = gm_l3;
                gm_l10 = t35;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t37 = gm_l11;
                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t39 = (t37 < t38);
                    if (!t39) goto lb36;
                    {
                        uint64_t t42 = gm_l11;
                        uint64_t t43 = (uintptr_t)t42;
                        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_tpff59ptr3s92 t47 = GM_LD(gm_tpff59ptr3s92, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t43 * 192));
                        F.l12 = t47;
                        {
                            gm_tq6gu4l68u3rv t49;
                            uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 0) + 0));
                            switch (t55) { case ((uint8_t)0ULL): goto ma51; case ((uint8_t)1ULL): goto ma52; case ((uint8_t)2ULL): goto ma53; default: goto ma54; }
ma51:;
                            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 0) + 0));
                            if (t57 != ((uint8_t)0ULL)) goto mn56;
                            gm_tfbvnc90lci8r t58 = GM_LD(gm_tfbvnc90lci8r, ((((uint8_t *)&F.l12) + 0) + 8));
                            F.l13 = t58;
                            void * t59 = gm_l0;
                            void * t60 = (void *)((uint8_t *)&F.l13);
                            gm_tfbvnc90lci8r t61;
                            gm_f_hoist_iter_value_cs229ip((void *)&t61, (void *)NULL, cx, NULL, t59, t60, ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_ll40;
                            }
                            gm_tq6gu4l68u3rv t62;
                            memset(&t62, 0, sizeof t62);
                            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_tfbvnc90lci8r, ((uint8_t *)&t62 + 8), t61);
                            t49 = t62;
                            goto me50;
mn56:;
ma52:;
                            uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 0) + 0));
                            if (t64 != ((uint8_t)1ULL)) goto mn63;
                            gm_t08olq97rscn3 t65 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l12) + 0) + 8) + 0));
                            gm_l14 = t65;
                            gm_tfbvnc90lci8r t66 = GM_LD(gm_tfbvnc90lci8r, (((((uint8_t *)&F.l12) + 0) + 8) + 32));
                            F.l15 = t66;
                            gm_t2tlhjbsqg5eb t67;
                            memcpy((uint8_t *)&t67 + 0, ((uint8_t *)&gm_l14), 32);
                            void * t68 = gm_l0;
                            void * t69 = (void *)((uint8_t *)&F.l15);
                            gm_f_hoist_iter_value_cs229ip((void *)&(*(gm_tfbvnc90lci8r *)((uint8_t *)&t67 + 32)), (void *)NULL, cx, NULL, t68, t69, ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_ll40;
                            }
                            gm_tq6gu4l68u3rv t70;
                            GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_t2tlhjbsqg5eb, ((uint8_t *)&t70 + 8), t67);
                            t49 = t70;
                            goto me50;
mn63:;
ma53:;
                            uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 0) + 0));
                            if (t72 != ((uint8_t)2ULL)) goto mn71;
                            gm_tfbvnc90lci8r t73 = GM_LD(gm_tfbvnc90lci8r, ((((uint8_t *)&F.l12) + 0) + 8));
                            F.l16 = t73;
                            gm_tfbvnc90lci8r t74 = F.l16;
                            gm_tq6gu4l68u3rv t75;
                            memset(&t75, 0, sizeof t75);
                            GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_tfbvnc90lci8r, ((uint8_t *)&t75 + 8), t74);
                            t49 = t75;
                            goto me50;
mn71:;
ma54:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 266, 29);
me50:;
                            F.l17 = t49;
                        }
                        {
                            void * t76 = (void *)((uint8_t *)&F.l9);
                            gm_tpff59ptr3s92 t77;
                            memset((uint8_t *)&t77 + 188, 0, 4);
                            memcpy((uint8_t *)&t77 + 0, ((uint8_t *)&F.l17), 176);
                            gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 176));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t77 + 176, t78);
                            gm_tftb1odc739t5 t79 = gm_f_push_jis5lhu(t76, &t77);
                            gm_tftb1odc739t5 t80 = t79;
                            if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_srcoimgd0jo62fr, 271, 9);
                            gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
                        }
                        goto me45;
mn46:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 265, 5);
me45:;
                    }
lc36:;
                    {
                        uint64_t t83 = gm_l11;
                        uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                        uint64_t t86 = r84;
                        gm_l11 = t86;
                    }
                }
gm_ll40:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb36; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc36; }
                }
                goto gm_lad;
lb36:;
            }
        }
        {
            memset((uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 4, 0, 4);
            memset((uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 132, 0, 4);
            void * t87 = gm_l1;
            uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)t87) + 0));
            gm_tlljg61a2ctgq t89;
            memset((uint8_t *)&t89 + 25, 0, 7);
            gm_tq7s98ihonkju t90 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l5)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kq6mn01fhkq)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t91 = t90;
            if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 8, gm_srcoimgd0jo62fr, 273, 54);
            void * t92 = GM_LD(void *, (((uint8_t *)&t91) + 8));
            GM_ST(void *, (uint8_t *)&t89 + 0, t92);
            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t89 + 8, t93);
            uint8_t t94 = gm_l4;
            GM_ST(uint8_t, (uint8_t *)&t89 + 24, t94);
            gm_t6e9d80inoppc t95;
            memset(&t95, 0, sizeof t95);
            GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tlljg61a2ctgq, ((uint8_t *)&t95 + 8), t89);
            void * t96 = gm_l1;
            gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t96) + 120));
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 0, t88);
            GM_ST(gm_t6e9d80inoppc, (uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 8, t95);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tfbvnc90lci8r *)gm_out) + 120, t97);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
        {
            void * t99 = gm_l1;
            memmove(gm_out, ((uint8_t *)t99), 136);
            goto gm_fdone;
        }
ix2:;
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
__attribute__((aligned(4))) uint8_t gm_f_iter_stable_k9c87oo(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_iter_stable_k9c87oo F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t08olq97rscn3 gm_l2; /* id */
    void * gm_l3; /* x */
    void * gm_l4; /* base */
    gm_t9qkh0rsooc8d gm_l5; /* op */
    void * gm_l6; /* operand */
    void * gm_l7; /* lhs */
    void * gm_l8; /* rhs */
    void * gm_l9; /* callee */
    gm_b16_8 gm_l10; /* args */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    GM_STACK_CHECK(&F, 1432u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma4; case ((uint8_t)4ULL): goto ma4; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma4; case ((uint8_t)8ULL): goto ma4; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma11; case ((uint8_t)28ULL): goto ma11; case ((uint8_t)29ULL): goto ma11; case ((uint8_t)30ULL): goto ma11; case ((uint8_t)31ULL): goto ma11; case ((uint8_t)32ULL): goto ma11; case ((uint8_t)33ULL): goto ma11; case ((uint8_t)34ULL): goto ma11; case ((uint8_t)35ULL): goto ma11; case ((uint8_t)36ULL): goto ma11; case ((uint8_t)37ULL): goto ma11; case ((uint8_t)38ULL): goto ma11; case ((uint8_t)39ULL): goto ma11; case ((uint8_t)40ULL): goto ma11; case ((uint8_t)41ULL): goto ma11; case ((uint8_t)42ULL): goto ma11; case ((uint8_t)43ULL): goto ma11; case ((uint8_t)44ULL): goto ma11; case ((uint8_t)45ULL): goto ma11; case ((uint8_t)46ULL): goto ma11; case ((uint8_t)47ULL): goto ma11; case ((uint8_t)48ULL): goto ma11; case ((uint8_t)49ULL): goto ma11; case ((uint8_t)50ULL): goto ma11; default: goto ma11; }
ma4:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)0ULL)) goto pa16;
        goto po15;
pa16:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t19 != ((uint8_t)1ULL)) goto pa18;
        goto po15;
pa18:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)2ULL)) goto pa20;
        goto po15;
pa20:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t23 != ((uint8_t)3ULL)) goto pa22;
        goto po15;
pa22:;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t25 != ((uint8_t)4ULL)) goto pa24;
        goto po15;
pa24:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t27 != ((uint8_t)5ULL)) goto pa26;
        goto po15;
pa26:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t29 != ((uint8_t)7ULL)) goto pa28;
        goto po15;
pa28:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t31 != ((uint8_t)8ULL)) goto pa30;
        goto po15;
pa30:;
        goto mn14;
po15:;
        uint8_t t32 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn14:;
ma5:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t34 != ((uint8_t)6ULL)) goto mn33;
        gm_t08olq97rscn3 t35 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t35;
        void * t36 = gm_l0;
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_tqvu78rpi6q1h t38;
        gm_f_lookup_local_6f38t7p((void *)&t38, t36, t37);
        gm_tqvu78rpi6q1h t39 = t38;
        uint8_t t40;
        if (((((uint8_t *)&t39))[208] != 0)) goto mn42;
        t40 = ((uint8_t)1);
        goto me41;
mn42:;
        t40 = ((uint8_t)0);
        goto me41;
mn43:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 281, 30);
me41:;
        uint8_t t44 = 0;
        F.ret = t40;
        goto gm_fret;
        goto me3;
mn33:;
ma6:;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t46 != ((uint8_t)9ULL)) goto mn45;
        void * t47 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t47;
        void * t48 = gm_l0;
        void * t49 = gm_l3;
        uint8_t t50 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t51 = 0;
        F.ret = t50;
        goto gm_fret;
        goto me3;
mn45:;
ma7:;
        uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t53 != ((uint8_t)22ULL)) goto mn52;
        void * t54 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t54;
        void * t55 = gm_l0;
        void * t56 = gm_l4;
        uint8_t t57 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t58 = 0;
        F.ret = t57;
        goto gm_fret;
        goto me3;
mn52:;
ma8:;
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t60 != ((uint8_t)12ULL)) goto mn59;
        gm_t9qkh0rsooc8d t61 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l5 = t61;
        void * t62 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l6 = t62;
        uint8_t t63 = 0;
        gm_t9qkh0rsooc8d t64 = gm_l5;
        gm_t9qkh0rsooc8d t65;
        GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)0ULL));
        gm_t9qkh0rsooc8d t66;
        GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)1ULL));
        gm_t9qkh0rsooc8d t67;
        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)5ULL));
        gm_tk22flr9mh3ql t68;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t68 + 0), t65);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t68 + 1), t66);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t68 + 2), t67);
        gm_tk22flr9mh3ql t69 = t68;
        uint8_t t70 = 0;
        gm_t9qkh0rsooc8d t71 = t64;
        if (gm_eqlv5i3f25jk(((uint8_t *)&t69) + 0, ((uint8_t *)&t71))) t70 = 1;
        if (gm_eqlv5i3f25jk(((uint8_t *)&t69) + 1, ((uint8_t *)&t71))) t70 = 1;
        if (gm_eqlv5i3f25jk(((uint8_t *)&t69) + 2, ((uint8_t *)&t71))) t70 = 1;
        uint8_t t72 = (uint8_t)(t70);
        if (t72) {
            void * t73 = gm_l0;
            void * t74 = gm_l6;
            uint8_t t75 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t73, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t63 = t75;
        } else t63 = 0;
        uint8_t t76 = 0;
        F.ret = t63;
        goto gm_fret;
        goto me3;
mn59:;
ma9:;
        uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t78 != ((uint8_t)13ULL)) goto mn77;
        void * t79 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l7 = t79;
        void * t80 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l8 = t80;
        uint8_t t81 = 0;
        void * t82 = gm_l0;
        void * t83 = gm_l7;
        uint8_t t84 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t82, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t84) {
            void * t85 = gm_l0;
            void * t86 = gm_l8;
            uint8_t t87 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t81 = t87;
        } else t81 = 0;
        uint8_t t88 = 0;
        F.ret = t81;
        goto gm_fret;
        goto me3;
mn77:;
ma10:;
        uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t90 != ((uint8_t)19ULL)) goto mn89;
        void * t91 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l9 = t91;
        gm_b16_8 t92 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l10 = t92;
        {
            void * t95 = gm_l9;
            uint8_t t96;
            uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t95) + 8) + 0));
            if (t99 != ((uint8_t)8ULL)) goto mn98;
            t96 = ((uint8_t)0);
            goto me97;
mn98:;
            t96 = ((uint8_t)1);
            goto me97;
mn100:;
            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 288, 16);
me97:;
            if (!t96) goto ie94;
            {
                uint8_t t102 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix94;
ie94:;
ix94:;
        }
        {
            {
                gm_b16_8 t104 = gm_l10;
                gm_l11 = t104;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t106 = gm_l12;
                    uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t108 = (t106 < t107);
                    if (!t108) goto lb105;
                    {
                        uint64_t t111 = gm_l12;
                        uint64_t t112 = (uintptr_t)t111;
                        uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        gm_tpff59ptr3s92 t116 = GM_LD(gm_tpff59ptr3s92, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t112 * 192));
                        F.l13 = t116;
                        {
                            uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                            switch (t123) { case ((uint8_t)0ULL): goto ma119; case ((uint8_t)1ULL): goto ma120; case ((uint8_t)2ULL): goto ma121; default: goto ma122; }
ma119:;
                            uint8_t t125 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                            if (t125 != ((uint8_t)0ULL)) goto mn124;
                            gm_tfbvnc90lci8r t126 = GM_LD(gm_tfbvnc90lci8r, ((((uint8_t *)&F.l13) + 0) + 8));
                            F.l14 = t126;
                            void * t128 = gm_l0;
                            void * t129 = (void *)((uint8_t *)&F.l14);
                            uint8_t t130 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t128, t129);
                            if (gm_unw.kind) {
                                goto gm_ll109;
                            }
                            uint8_t t131 = (uint8_t)!t130;
                            if (!t131) goto ie127;
                            {
                                uint8_t t133 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix127;
ie127:;
ix127:;
                            goto me118;
mn124:;
ma120:;
                            uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                            if (t135 != ((uint8_t)1ULL)) goto mn134;
                            gm_tfbvnc90lci8r t136 = GM_LD(gm_tfbvnc90lci8r, (((((uint8_t *)&F.l13) + 0) + 8) + 32));
                            F.l15 = t136;
                            void * t138 = gm_l0;
                            void * t139 = (void *)((uint8_t *)&F.l15);
                            uint8_t t140 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t138, t139);
                            if (gm_unw.kind) {
                                goto gm_ll109;
                            }
                            uint8_t t141 = (uint8_t)!t140;
                            if (!t141) goto ie137;
                            {
                                uint8_t t143 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix137;
ie137:;
ix137:;
                            goto me118;
mn134:;
ma121:;
                            uint8_t t145 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                            if (t145 != ((uint8_t)2ULL)) goto mn144;
                            uint8_t t146 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                            goto me118;
mn144:;
ma122:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 290, 17);
me118:;
                        }
                        goto me114;
mn115:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 289, 13);
me114:;
                    }
lc105:;
                    {
                        uint64_t t148 = gm_l12;
                        uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
                        uint64_t t151 = r149;
                        gm_l12 = t151;
                    }
                }
gm_ll109:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb105; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc105; }
                }
                goto gm_lad;
lb105:;
            }
        }
        {
            uint8_t t152 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me3;
mn89:;
ma11:;
        void * t154 = gm_l0;
        void * t155 = gm_l1;
        uint8_t t156 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t154, t155);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t157 = 0;
        F.ret = t156;
        goto gm_fret;
        goto me3;
mn153:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 279, 5);
me3:;
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
__attribute__((aligned(4))) void gm_f_iterator_arg_r0if28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_iterator_arg_r0if28f F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint32_t gm_l4; /* y */
    uint8_t gm_l5; /* yields */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tiilmfd16v3fc gm_l8; /* x */
    void * gm_l9; /* inner */
    void * gm_l10; /* x */
    void * gm_l13; /* lit */
    GM_STACK_CHECK(&F, 2076u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)15ULL)) goto ie2;
        gm_tddtjfggdat0d t10 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t8) + 8));
        F.l3 = t10;
        {
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            gm_t9805qtonbsag t14 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t13) + 1956));
            gm_t9805qtonbsag t15 = t14;
            uint32_t t16;
            if (((((uint8_t *)&t15))[4] != 0)) {
                uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&t15) + 0));
                t16 = t17;
            } else {
                gm_tj2dlstgqla1c t18;
                memset(&t18, 0, sizeof t18);
                uint8_t t19 = 0;
                F.ret = t18;
                goto gm_fret;
            }
            gm_l4 = t16;
        }
        {
            uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
            uint8_t t22 = (t21 > ((uint64_t)0ULL));
            if (!t22) goto ie20;
            {
                gm_tj2dlstgqla1c t24;
                memset(&t24, 0, sizeof t24);
                uint8_t t25 = 0;
                F.ret = t24;
                goto gm_fret;
            }
            goto ix20;
ie20:;
ix20:;
        }
        {
            gm_l5 = ((uint8_t)0);
        }
        {
            {
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 24) + 0));
                gm_l6 = t27;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t29 = gm_l7;
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t31 = (t29 < t30);
                    if (!t31) goto lb28;
                    {
                        uint64_t t34 = gm_l7;
                        uint64_t t35 = (uintptr_t)t34;
                        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_tiilmfd16v3fc t39 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t35 * 24));
                        gm_l8 = t39;
                        {
                            uint32_t t42 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
                            uint32_t t43 = gm_l4;
                            uint8_t t44 = ((t42 == t43));
                            if (!t44) goto ie41;
                            {
                                gm_l5 = ((uint8_t)1);
                            }
                            goto ix41;
ie41:;
ix41:;
                        }
                        goto me37;
mn38:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 221, 5);
me37:;
                    }
lc28:;
                    {
                        uint64_t t47 = gm_l7;
                        uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                        uint64_t t50 = r48;
                        gm_l7 = t50;
                    }
                }
lb28:;
            }
        }
        {
            uint8_t t52 = gm_l5;
            uint8_t t53 = (uint8_t)!t52;
            if (!t53) goto ie51;
            {
                gm_tj2dlstgqla1c t55;
                memset(&t55, 0, sizeof t55);
                uint8_t t56 = 0;
                F.ret = t55;
                goto gm_fret;
            }
            goto ix51;
ie51:;
ix51:;
        }
        {
            void * t57 = gm_l1;
            gm_l9 = t57;
        }
        {
            for (;;) {
                void * t59 = gm_l9;
                uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t59) + 8) + 0));
                if (t60 != ((uint8_t)9ULL)) goto lb58;
                void * t61 = GM_LD(void *, ((((uint8_t *)t59) + 8) + 8));
                gm_l10 = t61;
                {
                    void * t64 = gm_l10;
                    gm_l9 = t64;
                }
lc58:;
            }
lb58:;
        }
        {
            void * t66 = gm_l9;
            uint8_t t67;
            uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t66) + 8) + 0));
            if (t70 != ((uint8_t)19ULL)) goto mn69;
            t67 = ((uint8_t)0);
            goto me68;
mn69:;
            t67 = ((uint8_t)1);
            goto me68;
mn71:;
            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 227, 8);
me68:;
            if (!t67) goto ie65;
            {
                gm_tj2dlstgqla1c t73;
                memset(&t73, 0, sizeof t73);
                uint8_t t74 = 0;
                F.ret = t73;
                goto gm_fret;
            }
            goto ix65;
ie65:;
ix65:;
        }
        {
            void * t75 = gm_l0;
            void * t76 = gm_l9;
            gm_f_hoist_iterator_call_5rmgsce((void *)&F.l11, (void *)NULL, cx, NULL, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            memset((uint8_t *)&F.l12 + 17, 0, 7);
            memset((uint8_t *)&F.l12 + 68, 0, 4);
            gm_b16_8 t77 = gm_f_empty_slice_ito0pap();
            GM_ST(gm_b16_8, (uint8_t *)&F.l12 + 0, t77);
            GM_ST(uint8_t, (uint8_t *)&F.l12 + 16, ((uint8_t)0));
            GM_ST(void *, (uint8_t *)&F.l12 + 24, ((void *)0));
            gm_th2lqbo77pvsc t78;
            memset(&t78, 0, sizeof t78);
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l12 + 32, t78);
            void * t79 = gm_l1;
            gm_tl874t2atnlh3 t80 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t79) + 120));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l12 + 56, t80);
        }
        {
            gm_t0kv05e068jrn t81;
            memset((uint8_t *)&t81 + 148, 0, 4);
            memcpy((uint8_t *)&t81 + 0, ((uint8_t *)&F.l12), 72);
            gm_b16_8 t82 = gm_f_empty_slice_3jt8vgk();
            GM_ST(gm_b16_8, (uint8_t *)&t81 + 72, t82);
            gm_tq7s98ihonkju t83 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tfbvnc90lci8r *)((uint8_t *)&F.l11)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0gn7sac5pt)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t84 = t83;
            if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 8, gm_srcoimgd0jo62fr, 232, 86);
            void * t85 = GM_LD(void *, (((uint8_t *)&t84) + 8));
            gm_tapdr2ag17fh7 t86;
            memset(&t86, 0, sizeof t86);
            GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)1ULL));
            GM_ST(void *, ((uint8_t *)&t86 + 8), t85);
            GM_ST(gm_tapdr2ag17fh7, (uint8_t *)&t81 + 88, t86);
            void * t87 = gm_l1;
            gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + 120));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t81 + 136, t88);
            gm_tq7s98ihonkju t89 = gm_f_new_cnt7p5u((void *)NULL, cx, NULL, &t81, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8upcoeidcs)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t90 = t89;
            if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 8, gm_srcoimgd0jo62fr, 232, 12);
            void * t91 = GM_LD(void *, (((uint8_t *)&t90) + 8));
            gm_l13 = t91;
        }
        {
            void * t92 = gm_l0;
            void * t93 = (void *)(((uint8_t *)t92) + 1248);
            void * t94 = gm_l1;
            gm_tl874t2atnlh3 t95 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t94) + 120));
            gm_tftb1odc739t5 t96 = gm_f_push_0mk7udh(t93, GM_CONV(gm_cseahvg8v7p0vs, t95));
            gm_tftb1odc739t5 t97 = t96;
            if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 1, gm_srcoimgd0jo62fr, 233, 5);
            gm_b0_1 t98 = GM_LD(gm_b0_1, (((uint8_t *)&t97) + 1));
        }
        {
            gm_tfbvnc90lci8r t99;
            memset((uint8_t *)&t99 + 4, 0, 4);
            memset((uint8_t *)&t99 + 132, 0, 4);
            void * t100 = gm_l1;
            uint32_t t101 = GM_LD(uint32_t, (((uint8_t *)t100) + 0));
            GM_ST(uint32_t, (uint8_t *)&t99 + 0, t101);
            void * t102 = gm_l13;
            gm_t6e9d80inoppc t103;
            memset(&t103, 0, sizeof t103);
            GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)36ULL));
            GM_ST(void *, ((uint8_t *)&t103 + 8), t102);
            GM_ST(gm_t6e9d80inoppc, (uint8_t *)&t99 + 8, t103);
            void * t104 = gm_l1;
            gm_tl874t2atnlh3 t105 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t104) + 120));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t99 + 120, t105);
            gm_tj2dlstgqla1c t106;
            GM_ST(uint8_t, (uint8_t *)&t106 + 137, 0);
            GM_ST(uint16_t, (uint8_t *)&t106 + 138, 0);
            GM_ST(uint32_t, (uint8_t *)&t106 + 140, 0);
            GM_ST(gm_tfbvnc90lci8r, ((uint8_t *)&t106), t99);
            ((uint8_t *)&t106)[136] = 1;
            uint8_t t107 = 0;
            F.ret = t106;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_tj2dlstgqla1c t109;
            memset(&t109, 0, sizeof t109);
            uint8_t t110 = 0;
            F.ret = t109;
            goto gm_fret;
        }
ix2:;
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
__attribute__((aligned(4))) uint8_t gm_f_iterator_call_as_value_00i5mch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_iterator_call_as_value_00i5mch F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e0 */
    void * gm_l2; /* v */
    gm_t88rcucg1b7uk gm_l3; /* param */
    void * gm_l4; /* e */
    void * gm_l5; /* x */
    void * gm_l6; /* tys */
    gm_b16_8 gm_l7; /* text */
    uint64_t gm_l8; /* i */
    uint64_t gm_l9; /* i */
    gm_b16_8 gm_l10; /* c */
    gm_b16_8 gm_l11; /* p */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 1309u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_l4 = t2;
    }
    {
        for (;;) {
            void * t4 = gm_l4;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            if (t5 != ((uint8_t)9ULL)) goto lb3;
            void * t6 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
            gm_l5 = t6;
            {
                void * t9 = gm_l5;
                gm_l4 = t9;
            }
lc3:;
        }
lb3:;
    }
    {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        void * t12 = (void *)(((uint8_t *)t11) + 8);
        gm_l6 = t12;
    }
    {
        uint8_t t14 = 0;
        uint8_t t15 = 0;
        void * t16 = gm_l4;
        uint8_t t17;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
        if (t20 != ((uint8_t)19ULL)) goto mn19;
        t17 = ((uint8_t)0);
        goto me18;
mn19:;
        t17 = ((uint8_t)1);
        goto me18;
mn21:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 195, 8);
me18:;
        if (!t17) {
            void * t22 = gm_l6;
            void * t23 = gm_l0;
            void * t24 = (void *)(((uint8_t *)t23) + 96);
            void * t25 = gm_l0;
            void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
            void * t27 = gm_l2;
            gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 0));
            gm_t88rcucg1b7uk t29 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t24, t26, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t30 = gm_f_is_unit_n83ttnb(t22, GM_CONV(gm_csguankko4lmhd, t29));
            uint8_t t31 = (uint8_t)!t30;
            t15 = t31;
        } else t15 = 1;
        if (!t15) {
            void * t32 = gm_l6;
            void * t33 = gm_l0;
            void * t34 = (void *)(((uint8_t *)t33) + 96);
            void * t35 = gm_l0;
            void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
            gm_t88rcucg1b7uk t37 = gm_l3;
            gm_t88rcucg1b7uk t38 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t34, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t39 = gm_f_is_unit_n83ttnb(t32, GM_CONV(gm_csguankko4lmhd, t38));
            t14 = t39;
        } else t14 = 1;
        if (!t14) goto ie13;
        {
            uint8_t t41 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t43 = gm_l4;
        gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t43) + 120));
        void * t45 = gm_l0;
        gm_b16_8 t46 = GM_LD(gm_b16_8, ((((uint8_t *)t45) + 960) + 0));
        gm_b16_8 t47 = t46;
        uint8_t t48 = 0;
        gm_tl874t2atnlh3 t49 = t44;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t47) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t47))) + i * 12, ((uint8_t *)&t49))) { t48 = 1; break; }
        uint8_t t50 = (uint8_t)(!t48);
        if (!t50) goto ie42;
        {
            uint8_t t52 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix42;
ie42:;
ix42:;
    }
    {
        void * t53 = gm_l0;
        void * t54 = GM_LD(void *, (((uint8_t *)t53) + 0));
        void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
        void * t56 = GM_LD(void *, (((uint8_t *)t55) + 0));
        void * t57 = gm_l4;
        uint32_t t58 = GM_LD(uint32_t, ((((uint8_t *)t57) + 120) + 0));
        void * t59 = gm_f_file_o15fqku(t56, t58);
        uint64_t t60 = GM_LD(uintptr_t, (((uint8_t *)t59) + 16) + 8);
        void * t61 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t59) + 16)));
        void * t62 = gm_l4;
        uint32_t t63 = GM_LD(uint32_t, ((((uint8_t *)t62) + 120) + 4));
        uint64_t t64 = (uint64_t)t63;
        uint64_t t65 = (uintptr_t)t64;
        void * t66 = gm_l4;
        uint32_t t67 = GM_LD(uint32_t, ((((uint8_t *)t66) + 120) + 8));
        uint64_t t68 = (uint64_t)t67;
        uint64_t t69 = (uintptr_t)t68;
        if (t65 > t69 || t69 > (uintptr_t)t60) gm_trap_slice(t65, t69, t60, gm_srcoimgd0jo62fr, 197, 13);
        gm_b16_8 t70;
        GM_ST(void *, (uint8_t *)&t70, (void *)(((uint8_t *)t61 + t65 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(t69 - t65));
        gm_l7 = t70;
    }
    {
        gm_b16_8 t72 = gm_l7;
        gm_b16_8 t73;
        GM_ST(void *, (uint8_t *)&t73, (void *)(gm_smooaouqmm8dj));
        GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t74 = gm_f_find_nab29co(t72, t73);
        gm_tf5srh4mp4b3i t75 = t74;
        if (!((((uint8_t *)&t75))[8] != 0)) goto ie71;
        uint64_t t76 = GM_LD(uint64_t, ((uint8_t *)&t75));
        gm_l8 = t76;
        {
            uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            void * t79 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
            uint64_t t80 = gm_l8;
            uint64_t t81 = (uintptr_t)t80;
            if ((uintptr_t)0 > t81 || t81 > (uintptr_t)t78) gm_trap_slice((uintptr_t)0, t81, t78, gm_srcoimgd0jo62fr, 198, 42);
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(((uint8_t *)t79 + (uintptr_t)0 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(t81 - (uintptr_t)0));
            gm_l7 = t82;
        }
        goto ix71;
ie71:;
ix71:;
    }
    {
        gm_b16_8 t84 = gm_l7;
        gm_b16_8 t85;
        GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t86 = gm_f_rfind_ai5s2js(t84, t85);
        gm_tf5srh4mp4b3i t87 = t86;
        if (!((((uint8_t *)&t87))[8] != 0)) goto ie83;
        uint64_t t88 = GM_LD(uint64_t, ((uint8_t *)&t87));
        gm_l9 = t88;
        {
            uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            void * t91 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
            uint64_t t92 = gm_l9;
            uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
            if (o94) gm_trap("integer overflow in `+`", gm_srcoimgd0jo62fr, 199, 48);
            uint64_t t95 = r93;
            uint64_t t96 = (uintptr_t)t95;
            if (t96 > (uintptr_t)t90 || (uintptr_t)t90 > (uintptr_t)t90) gm_trap_slice(t96, (uintptr_t)t90, t90, gm_srcoimgd0jo62fr, 199, 43);
            gm_b16_8 t97;
            GM_ST(void *, (uint8_t *)&t97, (void *)(((uint8_t *)t91 + t96 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)((uintptr_t)t90 - t96));
            gm_l7 = t97;
        }
        goto ix83;
ie83:;
ix83:;
    }
    {
        gm_b16_8 t98 = gm_l7;
        gm_b16_8 t99 = gm_f_trim_fdrfe6i(t98);
        gm_l10 = t99;
    }
    {
        void * t100 = gm_l0;
        void * t101 = gm_l0;
        void * t102 = (void *)(((uint8_t *)t101) + 96);
        void * t103 = gm_l0;
        void * t104 = GM_LD(void *, (((uint8_t *)t103) + 0));
        gm_t88rcucg1b7uk t105 = gm_l3;
        gm_t88rcucg1b7uk t106 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t102, t104, t105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t107 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t100, t106);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t107;
    }
    {
        void * t108 = gm_l0;
        void * t109 = gm_l4;
        gm_tl874t2atnlh3 t110 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t109) + 120));
        gm_t0uuhaljsf410 t111;
        gm_b16_8 t112 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t111 + 0, t112);
        gm_b16_8 t113 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t111 + 16, t113);
        gm_b16_8 t114 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t111 + 32, t114);
        gm_b16_8 t115 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t111 + 48, t115);
        gm_tabb710d9mq2e t116;
        gm_f_format_mpnc7rc((void *)&t116, (void *)NULL, cx, NULL, &t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t117 = t116;
        if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 8, gm_srcoimgd0jo62fr, 202, 22);
        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
        gm_b16_8 t119 = t118;
        gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
        gm_b0_1 t121 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t108, t110, t120);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l12 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t123 = gm_l12;
            void * t124 = gm_l0;
            uint64_t t125 = GM_LD(uintptr_t, ((((uint8_t *)t124) + 192) + 0) + 8);
            uint8_t t126 = (t123 < t125);
            if (!t126) goto lb122;
            {
                uint64_t t130 = gm_l12;
                uint64_t t131 = (uintptr_t)t130;
                void * t132 = gm_l0;
                uint64_t t133 = GM_LD(uintptr_t, ((((uint8_t *)t132) + 192) + 0) + 8);
                if (t131 >= (uintptr_t)(t133)) gm_trap_index(t131, t133, gm_srcoimgd0jo62fr, 205, 12);
                gm_tl874t2atnlh3 t134 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t132) + 192) + 0)) + t131 * 40) + 24));
                void * t135 = gm_l4;
                gm_tl874t2atnlh3 t136 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t135) + 120));
                gm_tl874t2atnlh3 t137 = t134;
                gm_tl874t2atnlh3 t138 = t136;
                uint8_t t139 = (gm_eqff85ac10o3(((uint8_t *)&t137), ((uint8_t *)&t138)));
                if (!t139) goto ie129;
                {
                    {
                        void * t142 = gm_l0;
                        void * t143 = (void *)(((uint8_t *)t142) + 192);
                        uint64_t t144 = gm_l12;
                        gm_tj44oivvogvtl t145;
                        gm_f_remove_vrfdutr((void *)&t145, t143, t144);
                    }
                }
                goto ix129;
ie129:;
                {
                    uint64_t t147 = gm_l12;
                    uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
                    if (o149) gm_trap("integer overflow in `+`", gm_srcoimgd0jo62fr, 208, 13);
                    uint64_t t150 = r148;
                    gm_l12 = t150;
                }
ix129:;
            }
lc122:;
        }
lb122:;
    }
    {
        uint8_t t151 = 0;
        F.ret = ((uint8_t)1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lift_temps_7c6gk6l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_lift_temps_7c6gk6l F;
    void * gm_l1; /* e */
    void * gm_l3; /* inner */
    uint32_t gm_l4; /* t */
    void * gm_l5; /* v */
    GM_STACK_CHECK(&F, 979u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    {
        void * t3 = gm_l1;
        uint8_t t4;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; default: goto ma8; }
ma6:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t12 != ((uint8_t)34ULL)) goto mn11;
        t4 = ((uint8_t)1);
        goto me5;
mn11:;
ma7:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t14 != ((uint8_t)27ULL)) goto mn13;
        t4 = ((uint8_t)1);
        goto me5;
mn13:;
ma8:;
        t4 = ((uint8_t)0);
        goto me5;
mn15:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 175, 8);
me5:;
        if (!t4) goto ie2;
        {
            gm_b0_1 t17;
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t20 = gm_l1;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t20) + 8) + 0));
        if (t21 != ((uint8_t)25ULL)) goto ie19;
        void * t22 = GM_LD(void *, ((((uint8_t *)t20) + 8) + 8));
        gm_l3 = t22;
        {
            void * t24 = F.l0;
            void * t25 = gm_l3;
            void * t26 = F.l2;
            gm_b0_1 t27 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t24, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t28 = F.l0;
            void * t29 = gm_l3;
            gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t29) + 0));
            void * t31 = gm_l3;
            gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t31) + 208));
            uint32_t t33 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t28, t30, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t33;
        }
        {
            void * t34 = gm_l3;
            gm_tq7s98ihonkju t35 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t34)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kmnip3ksasl)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t36 = t35;
            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_srcoimgd0jo62fr, 179, 14);
            void * t37 = GM_LD(void *, (((uint8_t *)&t36) + 8));
            gm_l5 = t37;
        }
        {
            void * t38 = gm_l3;
            uint8_t *t39 = ((uint8_t *)t38);
            void * t40 = gm_l3;
            gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t40) + 0));
            uint32_t t42 = gm_l4;
            gm_t5brlu7fal70j t43;
            memset(&t43, 0, sizeof t43);
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)15ULL));
            GM_ST(uint32_t, ((uint8_t *)&t43 + 8), t42);
            void * t44 = gm_l3;
            gm_tl874t2atnlh3 t45 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t44) + 208));
            void * t46 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t41, &t43, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tfpf0s89fpb3r t47 = GM_LD(gm_tfpf0s89fpb3r, ((uint8_t *)t46));
            GM_ST(gm_tfpf0s89fpb3r, t39, t47);
        }
        {
            void * t48 = F.l2;
            uint32_t t49 = gm_l4;
            void * t50 = gm_l5;
            gm_ttggq7ng4kp3p t51;
            GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t51 + 0), t49);
            GM_ST(void *, ((uint8_t *)&t51 + 8), t50);
            gm_trcmmd12cefb1 t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t52 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)0ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t52 + 8), t51);
            gm_tftb1odc739t5 t53 = gm_f_push_d6c76ud_2(t48, &t52);
            gm_tftb1odc739t5 t54 = t53;
            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcoimgd0jo62fr, 181, 9);
            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
        }
        {
            void * t56 = gm_l1;
            void * t57 = gm_l3;
            gm_t5brlu7fal70j t58;
            memset(&t58, 0, sizeof t58);
            GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)24ULL));
            GM_ST(void *, ((uint8_t *)&t58 + 8), t57);
            GM_ST(gm_t5brlu7fal70j, (((uint8_t *)t56) + 8), t58);
        }
        {
            gm_b0_1 t59;
            uint8_t t60 = 0;
            F.ret = t59;
            goto gm_fret;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t61 = gm_l1;
        gm_closure t62 = ((gm_closure){(void *)gm_f_fn_t9suqla, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t61, t62);
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
inline __attribute__((aligned(4))) void * gm_f_local_c3kjspo(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_local_c3kjspo F;
    uint32_t gm_l0; /* id */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK(&F, 252u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t88rcucg1b7uk t1 = gm_l1;
    uint32_t t2 = gm_l0;
    gm_t5brlu7fal70j t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 8), t2);
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&((struct gm_fr_enumerate_loop_5f6m89f *)((&F.h)->up))->l12));
    void * t5 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t1, &t3, t4);
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
inline __attribute__((aligned(4))) void * gm_f_not_an_iterator_au10b3l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_not_an_iterator_au10b3l F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    void * gm_l2; /* call */
    void * gm_l3; /* e */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l1;
        void * t5 = gm_l2;
        void * t6 = gm_f_for_each_value_5qklbg7((void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t7 = t6;
        if (!(GM_LD(void *, ((uint8_t *)&t7)) != 0)) goto ie2;
        void * t8 = GM_LD(void *, ((uint8_t *)&t7));
        gm_l3 = t8;
        {
            void * t10 = gm_l3;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        void * t16 = gm_l2;
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t16) + 0));
        uint8_t t18 = gm_f_is_error_8k27gd1(t15, GM_CONV(gm_csguankko4lmhd, t17));
        uint8_t t19 = (uint8_t)!t18;
        if (!t19) goto ie12;
        {
            void * t21 = gm_l0;
            void * t22 = gm_l1;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 56));
            gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t23) + 120));
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sb8airrqr74u5));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(72));
            gm_b0_1 t26 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t21, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
        void * t29 = gm_l1;
        gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t29) + 104));
        void * t31 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t28, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t32 = 0;
        F.ret = t31;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_place_iter_pre_qn5c5b5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint64_t a3, void * a4) {
    struct gm_fr_place_iter_pre_qn5c5b5 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* bound */
    void * gm_l2; /* written */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* stmts */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* j */
    void * gm_l8; /* v */
    GM_STACK_CHECK(&F, 434u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1160) + 0) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            {
                void * t8 = gm_l2;
                gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 0));
                gm_l5 = t9;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t11 = gm_l6;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t13 = (t11 < t12);
                    if (!t13) goto lb10;
                    {
                        uint64_t t16 = gm_l6;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t17 * 8));
                        uint64_t t20 = t19;
                        uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&t20));
                        gm_l7 = t23;
                        {
                            uint64_t t26 = gm_l7;
                            uint64_t t27 = (uintptr_t)t26;
                            void * t28 = gm_l1;
                            uint64_t t29 = GM_LD(uintptr_t, (((uint8_t *)t28) + 0) + 8);
                            if (t27 >= (uintptr_t)(t29)) gm_trap_index(t27, t29, gm_srcoimgd0jo62fr, 244, 21);
                            void * t30 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)t28) + 0)) + t27 * 8));
                            void * t31 = t30;
                            if (!(GM_LD(void *, ((uint8_t *)&t31)) != 0)) goto ie25;
                            void * t32 = GM_LD(void *, ((uint8_t *)&t31));
                            gm_l8 = t32;
                            {
                                uint64_t t34 = gm_l7;
                                uint64_t t35 = (uintptr_t)t34;
                                void * t36 = gm_l1;
                                uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)t36) + 0) + 8);
                                if (t35 >= (uintptr_t)(t37)) gm_trap_index(t35, t37, gm_srcoimgd0jo62fr, 244, 38);
                                uint8_t *t38 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t36) + 0)) + t35 * 8);
                                void * t39 = gm_l0;
                                void * t40 = gm_l8;
                                void * t41 = gm_l4;
                                void * t42 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t39, t40, t41);
                                if (gm_unw.kind) {
                                    goto gm_ll14;
                                }
                                GM_ST(void *, t38, t42);
                            }
                            goto ix25;
ie25:;
ix25:;
                        }
                        goto me21;
mn22:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 243, 9);
me21:;
                    }
lc10:;
                    {
                        uint64_t t44 = gm_l6;
                        uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                        uint64_t t47 = r45;
                        gm_l6 = t47;
                    }
                }
gm_ll14:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb10; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc10; }
                }
                goto gm_lad;
lb10:;
            }
        }
        {
            void * t48 = gm_l4;
            void * t49 = gm_l0;
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((((uint8_t *)t49) + 1160) + 0));
            gm_b16_8 t51 = t50;
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
            gm_tftb1odc739t5 t53 = gm_f_extend_nkql463(t48, t52);
            gm_tftb1odc739t5 t54 = t53;
            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcoimgd0jo62fr, 246, 9);
            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
        }
        {
            void * t56 = gm_l0;
            uint8_t *t57 = (((uint8_t *)t56) + 1160);
            gm_f_list_ir0h3tc_2((void *)&(*(gm_trqfvmjtt4sij *)t57), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t58 = gm_l2;
        uint64_t t59 = gm_l3;
        gm_tftb1odc739t5 t60 = gm_f_push_ucgmlo2(t58, t59);
        gm_tftb1odc739t5 t61 = t60;
        if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_srcoimgd0jo62fr, 249, 5);
        gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
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
