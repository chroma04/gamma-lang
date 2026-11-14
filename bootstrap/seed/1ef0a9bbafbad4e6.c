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
typedef struct { _Alignas(8) uint8_t b[16]; } gm_b16_8;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
struct gm_fr_describe_rhl0asi {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_punct_fdeminj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_keyword_from_1ofmh1u {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_keyword_text_149oqn4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_punct_text_ov4r40u {
    gm_frame h;
    gm_b16_8 ret;
};
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_rhl0asi(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6o2k0r9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_g23e5qn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_njg1saa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r31rqga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj(const void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj_c(gm_t3fcbuehh3bs2 a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_keyword_from_1ofmh1u(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_keyword_text_149oqn4(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_punct_text_ov4r40u(gm_t9qkh0rsooc8d a0);
extern const uint8_t gm_s02ru83e3fffc[3];
extern const uint8_t gm_s0eemp7of8buc[3];
extern const uint8_t gm_s0s4ef50964r9[3];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1c4sfff8ac39[7];
extern const uint8_t gm_s1dbslfck7laf[3];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s277jq8ulrihr[7];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s33pl04oikskr[5];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3bkmscn5fi56[3];
extern const uint8_t gm_s4fd35uidvgkn[4];
extern const uint8_t gm_s4g3cajt15dmi[3];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s5mu2shl2bmft[14];
extern const uint8_t gm_s5rv6umk4akl0[6];
extern const uint8_t gm_s66uvortgrtsg[4];
extern const uint8_t gm_s6aiq6b6gsv1o[18];
extern const uint8_t gm_s6lcmor84nt1l[4];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s8av3ru5o85v4[6];
extern const uint8_t gm_s8m5q4i8uoe5j[6];
extern const uint8_t gm_s8p1h96ji9kh0[6];
extern const uint8_t gm_s920dpvvv6cej[4];
extern const uint8_t gm_s9es2tos1bhfp[5];
extern const uint8_t gm_s9hi985r6f61m[3];
extern const uint8_t gm_s9s0gbnmtpvjl[4];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sabc7m3rug04j[3];
extern const uint8_t gm_sad0udrpgk27o[3];
extern const uint8_t gm_saimepa8bfkkm[3];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_sb88rajuutaep[4];
extern const uint8_t gm_sbjiv038rtlcj[4];
extern const uint8_t gm_sc49g68j2cekh[7];
extern const uint8_t gm_sc8o4oaph9gbr[2];
extern const uint8_t gm_scc8p0a9eachg[4];
extern const uint8_t gm_scvhpklutvpjg[4];
extern const uint8_t gm_sd7p31mtitiq9[3];
extern const uint8_t gm_sdavnmflj0bfl[4];
extern const uint8_t gm_sde2pekt9oc5v[4];
extern const uint8_t gm_sdeq555foclko[3];
extern const uint8_t gm_seccpktgmrevt[2];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_ser8ip7vtbv6c[9];
extern const uint8_t gm_sf9a2kg3eo1gs[2];
extern const uint8_t gm_sfg1e0lbt8sf5[3];
extern const uint8_t gm_sfi2ha42r1nr2[2];
extern const uint8_t gm_sfqk3rro4g7t0[2];
extern const uint8_t gm_sg3kpa3h16no3[7];
extern const uint8_t gm_sgemh066sqogf[4];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_sh0j2ggs1vvoa[2];
extern const uint8_t gm_sh40na9qbga3l[2];
extern const uint8_t gm_shge5n743ua4h[3];
extern const uint8_t gm_shtiokhdiebr1[3];
extern const uint8_t gm_shttb5kqse295[12];
extern const uint8_t gm_si1m7q6ia0hto[7];
extern const uint8_t gm_si34mhnsv5dtr[8];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sidtn67sdk4e0[7];
extern const uint8_t gm_sj0kptrfbec99[3];
extern const uint8_t gm_sj5j224m4ulf9[3];
extern const uint8_t gm_sjfg2291kjugh[4];
extern const uint8_t gm_sjid61i849qic[3];
extern const uint8_t gm_sjjig60ja37ka[3];
extern const uint8_t gm_sjqs7q3ostqbm[4];
extern const uint8_t gm_sk754mj3p1el6[2];
extern const uint8_t gm_skkvljkq130ke[15];
extern const uint8_t gm_skm1g73974f87[4];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_skriisqpofdin[3];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl4co37h2cmmb[3];
extern const uint8_t gm_sl9nvct67or45[3];
extern const uint8_t gm_sll7gknpe999d[4];
extern const uint8_t gm_sllcm1pqagh28[6];
extern const uint8_t gm_slpo22sjs83b1[5];
extern const uint8_t gm_sm3gke457vdm3[5];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_sm8obe455p2d9[3];
extern const uint8_t gm_smbls870q1nm0[16];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smu48daue2qai[6];
extern const uint8_t gm_sn6sqlran7h3r[7];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_soqt3jnmovq5l[4];
extern const uint8_t gm_sp3rdossibc7f[4];
extern const uint8_t gm_spi47lg4tr80j[3];
extern const uint8_t gm_sq1oij2hrovtv[4];
extern const uint8_t gm_squedv51820an[5];
extern const uint8_t gm_sr31o7m0aohvd[9];
extern const char gm_src41vqrhgekac9[];
extern const uint8_t gm_srcsoo8lineh7[3];
extern const uint8_t gm_srri8c6al1v1g[5];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_ss67f0gmfen1m[3];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_su9718bmbci33[4];
extern const uint8_t gm_svbhcvlahd4n2[3];
extern const uint8_t gm_svkleo8hhv87n[4];
extern const uint8_t gm_svm0rfaqk3b2q[4];
extern const uint8_t gm_svqdrhms9i231[7];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj(const void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_punct_fdeminj F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* p */
    gm_t9qkh0rsooc8d gm_l2; /* q */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    uint8_t t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)8ULL)) goto mn3;
    gm_t9qkh0rsooc8d t5 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l2 = t5;
    gm_t9qkh0rsooc8d t6 = gm_l2;
    gm_t9qkh0rsooc8d t7 = gm_l1;
    gm_t9qkh0rsooc8d t8 = t6;
    gm_t9qkh0rsooc8d t9 = t7;
    uint8_t t10 = (gm_eqv3tj1pu43g(((uint8_t *)&t8), ((uint8_t *)&t9)));
    t1 = t10;
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn11:;
    gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 308, 51);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_rhl0asi(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_describe_rhl0asi F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_b16_8 gm_l1; /* s */
    gm_t9qkh0rsooc8d gm_l2; /* kw */
    void * gm_l3; /* l */
    gm_b16_8 gm_l4; /* s */
    gm_b16_8 gm_l5; /* s */
    gm_t9qkh0rsooc8d gm_l6; /* p */
    GM_STACK_CHECK(&F, 1019u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t15) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; default: goto ma14; }
ma3:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t17 != ((uint8_t)0ULL)) goto mn16;
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l1 = t18;
    gm_b16_8 t19 = gm_l1;
    gm_tnpifv20irnph t20;
    GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t19);
    gm_tabb710d9mq2e t21;
    gm_f_format_6o2k0r9((void *)&t21, (void *)NULL, cx, NULL, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t22 = t21;
    if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_src41vqrhgekac9, 315, 18);
    gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
    gm_b16_8 t24 = t23;
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
    t1 = t25;
    goto me2;
mn16:;
ma4:;
    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t27 != ((uint8_t)1ULL)) goto mn26;
    gm_b16_8 t28;
    GM_ST(void *, (uint8_t *)&t28, (void *)(gm_s6lcmor84nt1l));
    GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(3));
    t1 = t28;
    goto me2;
mn26:;
ma5:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t30 != ((uint8_t)2ULL)) goto mn29;
    gm_t9qkh0rsooc8d t31 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l2 = t31;
    gm_t9qkh0rsooc8d t32 = gm_l2;
    gm_b16_8 t33 = gm_f_keyword_text_149oqn4(t32);
    gm_tnpifv20irnph t34;
    GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t33);
    gm_tabb710d9mq2e t35;
    gm_f_format_g23e5qn((void *)&t35, (void *)NULL, cx, NULL, t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t36 = t35;
    if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_src41vqrhgekac9, 317, 21);
    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
    gm_b16_8 t38 = t37;
    gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
    t1 = t39;
    goto me2;
mn29:;
ma6:;
    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t41 != ((uint8_t)3ULL)) goto mn40;
    void * t42 = GM_LD(void *, (((uint8_t *)a0) + 8));
    gm_l3 = t42;
    gm_b16_8 t43;
    void * t45 = gm_l3;
    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)t45) + 60));
    if (!t46) goto ie44;
    gm_b16_8 t48;
    GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s6aiq6b6gsv1o));
    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(17));
    gm_b16_8 t49 = t48;
    t43 = t49;
    goto ix44;
ie44:;
    gm_b16_8 t51;
    GM_ST(void *, (uint8_t *)&t51, (void *)(gm_smbls870q1nm0));
    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(15));
    gm_b16_8 t52 = t51;
    t43 = t52;
ix44:;
    t1 = t43;
    goto me2;
mn40:;
ma7:;
    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t54 != ((uint8_t)4ULL)) goto mn53;
    gm_b16_8 t55;
    GM_ST(void *, (uint8_t *)&t55, (void *)(gm_s5mu2shl2bmft));
    GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(13));
    t1 = t55;
    goto me2;
mn53:;
ma8:;
    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t57 != ((uint8_t)5ULL)) goto mn56;
    gm_b16_8 t58;
    GM_ST(void *, (uint8_t *)&t58, (void *)(gm_skkvljkq130ke));
    GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(14));
    t1 = t58;
    goto me2;
mn56:;
ma9:;
    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t60 != ((uint8_t)6ULL)) goto mn59;
    gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l4 = t61;
    gm_b16_8 t62 = gm_l4;
    gm_tnpifv20irnph t63;
    GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t62);
    gm_tabb710d9mq2e t64;
    gm_f_format_njg1saa((void *)&t64, (void *)NULL, cx, NULL, t63);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t65 = t64;
    if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 8, gm_src41vqrhgekac9, 321, 22);
    gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 8));
    gm_b16_8 t67 = t66;
    gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
    t1 = t68;
    goto me2;
mn59:;
ma10:;
    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t70 != ((uint8_t)7ULL)) goto mn69;
    gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l5 = t71;
    gm_b16_8 t72 = gm_l5;
    gm_tnpifv20irnph t73;
    GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
    gm_tabb710d9mq2e t74;
    gm_f_format_r31rqga((void *)&t74, (void *)NULL, cx, NULL, t73);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t75 = t74;
    if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_src41vqrhgekac9, 322, 22);
    gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
    gm_b16_8 t77 = t76;
    gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
    t1 = t78;
    goto me2;
mn69:;
ma11:;
    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t80 != ((uint8_t)8ULL)) goto mn79;
    gm_t9qkh0rsooc8d t81 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l6 = t81;
    gm_t9qkh0rsooc8d t82 = gm_l6;
    gm_b16_8 t83 = gm_f_punct_text_ov4r40u(t82);
    gm_tnpifv20irnph t84;
    GM_ST(gm_b16_8, ((uint8_t *)&t84 + 0), t83);
    gm_tabb710d9mq2e t85;
    gm_f_format_h4jcodi((void *)&t85, (void *)NULL, cx, NULL, t84);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t86 = t85;
    if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 8, gm_src41vqrhgekac9, 323, 18);
    gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
    gm_b16_8 t88 = t87;
    gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
    t1 = t89;
    goto me2;
mn79:;
ma12:;
    uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t91 != ((uint8_t)9ULL)) goto mn90;
    gm_b16_8 t92;
    GM_ST(void *, (uint8_t *)&t92, (void *)(gm_si34mhnsv5dtr));
    GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(7));
    t1 = t92;
    goto me2;
mn90:;
ma13:;
    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t94 != ((uint8_t)10ULL)) goto mn93;
    gm_b16_8 t95;
    GM_ST(void *, (uint8_t *)&t95, (void *)(gm_shttb5kqse295));
    GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(11));
    t1 = t95;
    goto me2;
mn93:;
ma14:;
    gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 314, 59);
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj(const void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj_c(gm_t3fcbuehh3bs2 a0, gm_t9qkh0rsooc8d a1) { return gm_f_is_punct_fdeminj(&a0, a1); }
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_keyword_from_1ofmh1u(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_keyword_from_1ofmh1u F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t4 < ((uint64_t)2ULL));
        if (!t5) {
            uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t7 = (t6 > ((uint64_t)8ULL));
            t3 = t7;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_taari1s0m51a7 t9;
            memset(&t9, 0, sizeof t9);
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_src41vqrhgekac9, 19, 11);
        uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t11 * 1));
        uint8_t t14 = t13;
        uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t17 == ((uint8_t)97ULL))) goto mn16;
        uint8_t t19 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s920dpvvv6cej, 3) == 0));
        if (!t19) goto ie18;
        {
            gm_t9qkh0rsooc8d t21;
            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0ULL));
            gm_taari1s0m51a7 t22;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22), t21);
            ((uint8_t *)&t22)[1] = 1;
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
        goto me15;
mn16:;
        uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t25 == ((uint8_t)98ULL))) goto mn24;
        uint8_t t27 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s8av3ru5o85v4, 5) == 0));
        if (!t27) goto ie26;
        {
            gm_t9qkh0rsooc8d t29;
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)1ULL));
            gm_taari1s0m51a7 t30;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t30), t29);
            ((uint8_t *)&t30)[1] = 1;
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
        goto me15;
mn24:;
        uint8_t t33 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t33 == ((uint8_t)99ULL))) goto mn32;
        gm_taari1s0m51a7 t34;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_si1m7q6ia0hto, 6) == 0)) goto mn36;
        gm_t9qkh0rsooc8d t37;
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)2ULL));
        gm_taari1s0m51a7 t38;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38), t37);
        ((uint8_t *)&t38)[1] = 1;
        t34 = t38;
        goto me35;
mn36:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sr31o7m0aohvd, 8) == 0)) goto mn39;
        gm_t9qkh0rsooc8d t40;
        GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)3ULL));
        gm_taari1s0m51a7 t41;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t41), t40);
        ((uint8_t *)&t41)[1] = 1;
        t34 = t41;
        goto me35;
mn39:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s66uvortgrtsg, 3) == 0)) goto mn42;
        gm_t9qkh0rsooc8d t43;
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)4ULL));
        gm_taari1s0m51a7 t44;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t44), t43);
        ((uint8_t *)&t44)[1] = 1;
        t34 = t44;
        goto me35;
mn42:;
        gm_taari1s0m51a7 t46;
        memset(&t46, 0, sizeof t46);
        t34 = t46;
        goto me35;
mn45:;
        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 22, 23);
me35:;
        uint8_t t47 = 0;
        F.ret = t34;
        goto gm_fret;
        goto me15;
mn32:;
        uint8_t t49 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t49 == ((uint8_t)100ULL))) goto mn48;
        uint8_t t51 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sllcm1pqagh28, 5) == 0));
        if (!t51) goto ie50;
        {
            gm_t9qkh0rsooc8d t53;
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)5ULL));
            gm_taari1s0m51a7 t54;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54), t53);
            ((uint8_t *)&t54)[1] = 1;
            uint8_t t55 = 0;
            F.ret = t54;
            goto gm_fret;
        }
        goto ix50;
ie50:;
ix50:;
        goto me15;
mn48:;
        uint8_t t57 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t57 == ((uint8_t)101ULL))) goto mn56;
        gm_taari1s0m51a7 t58;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_svqdrhms9i231, 6) == 0)) goto mn60;
        gm_t9qkh0rsooc8d t61;
        GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)6ULL));
        gm_taari1s0m51a7 t62;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t62), t61);
        ((uint8_t *)&t62)[1] = 1;
        t58 = t62;
        goto me59;
mn60:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s9es2tos1bhfp, 4) == 0)) goto mn63;
        gm_t9qkh0rsooc8d t64;
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)7ULL));
        gm_taari1s0m51a7 t65;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t65), t64);
        ((uint8_t *)&t65)[1] = 1;
        t58 = t65;
        goto me59;
mn63:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_ser8ip7vtbv6c, 8) == 0)) goto mn66;
        gm_t9qkh0rsooc8d t67;
        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)8ULL));
        gm_taari1s0m51a7 t68;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t68), t67);
        ((uint8_t *)&t68)[1] = 1;
        t58 = t68;
        goto me59;
mn66:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sb88rajuutaep, 3) == 0)) goto mn69;
        gm_t9qkh0rsooc8d t70;
        GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)9ULL));
        gm_taari1s0m51a7 t71;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71), t70);
        ((uint8_t *)&t71)[1] = 1;
        t58 = t71;
        goto me59;
mn69:;
        gm_taari1s0m51a7 t73;
        memset(&t73, 0, sizeof t73);
        t58 = t73;
        goto me59;
mn72:;
        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 29, 23);
me59:;
        uint8_t t74 = 0;
        F.ret = t58;
        goto gm_fret;
        goto me15;
mn56:;
        uint8_t t76 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t76 == ((uint8_t)102ULL))) goto mn75;
        gm_taari1s0m51a7 t77;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sogehh8t1m7tn, 5) == 0)) goto mn79;
        gm_t9qkh0rsooc8d t80;
        GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)10ULL));
        gm_taari1s0m51a7 t81;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t81), t80);
        ((uint8_t *)&t81)[1] = 1;
        t77 = t81;
        goto me78;
mn79:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s579fb7l5l6df, 2) == 0)) goto mn82;
        gm_t9qkh0rsooc8d t83;
        GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)11ULL));
        gm_taari1s0m51a7 t84;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t84), t83);
        ((uint8_t *)&t84)[1] = 1;
        t77 = t84;
        goto me78;
mn82:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_scc8p0a9eachg, 3) == 0)) goto mn85;
        gm_t9qkh0rsooc8d t86;
        GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)12ULL));
        gm_taari1s0m51a7 t87;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t87), t86);
        ((uint8_t *)&t87)[1] = 1;
        t77 = t87;
        goto me78;
mn85:;
        gm_taari1s0m51a7 t89;
        memset(&t89, 0, sizeof t89);
        t77 = t89;
        goto me78;
mn88:;
        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 36, 23);
me78:;
        uint8_t t90 = 0;
        F.ret = t77;
        goto gm_fret;
        goto me15;
mn75:;
        uint8_t t92 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t92 == ((uint8_t)105ULL))) goto mn91;
        gm_taari1s0m51a7 t93;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sl9nvct67or45, 2) == 0)) goto mn95;
        gm_t9qkh0rsooc8d t96;
        GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)13ULL));
        gm_taari1s0m51a7 t97;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t97), t96);
        ((uint8_t *)&t97)[1] = 1;
        t93 = t97;
        goto me94;
mn95:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sg3kpa3h16no3, 6) == 0)) goto mn98;
        gm_t9qkh0rsooc8d t99;
        GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)14ULL));
        gm_taari1s0m51a7 t100;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t100), t99);
        ((uint8_t *)&t100)[1] = 1;
        t93 = t100;
        goto me94;
mn98:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_svbhcvlahd4n2, 2) == 0)) goto mn101;
        gm_t9qkh0rsooc8d t102;
        GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)15ULL));
        gm_taari1s0m51a7 t103;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t103), t102);
        ((uint8_t *)&t103)[1] = 1;
        t93 = t103;
        goto me94;
mn101:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sgpdmvc9s0d2p, 2) == 0)) goto mn104;
        gm_t9qkh0rsooc8d t105;
        GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)16ULL));
        gm_taari1s0m51a7 t106;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t106), t105);
        ((uint8_t *)&t106)[1] = 1;
        t93 = t106;
        goto me94;
mn104:;
        gm_taari1s0m51a7 t108;
        memset(&t108, 0, sizeof t108);
        t93 = t108;
        goto me94;
mn107:;
        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 42, 23);
me94:;
        uint8_t t109 = 0;
        F.ret = t93;
        goto gm_fret;
        goto me15;
mn91:;
        uint8_t t111 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t111 == ((uint8_t)109ULL))) goto mn110;
        uint8_t t113 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s8m5q4i8uoe5j, 5) == 0));
        if (!t113) goto ie112;
        {
            gm_t9qkh0rsooc8d t115;
            GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)17ULL));
            gm_taari1s0m51a7 t116;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t116), t115);
            ((uint8_t *)&t116)[1] = 1;
            uint8_t t117 = 0;
            F.ret = t116;
            goto gm_fret;
        }
        goto ix112;
ie112:;
ix112:;
        goto me15;
mn110:;
        uint8_t t119 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t119 == ((uint8_t)110ULL))) goto mn118;
        gm_taari1s0m51a7 t120;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_scvhpklutvpjg, 3) == 0)) goto mn122;
        gm_t9qkh0rsooc8d t123;
        GM_ST(uint8_t, ((uint8_t *)&t123 + 0), ((uint8_t)18ULL));
        gm_taari1s0m51a7 t124;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t124), t123);
        ((uint8_t *)&t124)[1] = 1;
        t120 = t124;
        goto me121;
mn122:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_squedv51820an, 4) == 0)) goto mn125;
        gm_t9qkh0rsooc8d t126;
        GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)19ULL));
        gm_taari1s0m51a7 t127;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t127), t126);
        ((uint8_t *)&t127)[1] = 1;
        t120 = t127;
        goto me121;
mn125:;
        gm_taari1s0m51a7 t129;
        memset(&t129, 0, sizeof t129);
        t120 = t129;
        goto me121;
mn128:;
        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 50, 23);
me121:;
        uint8_t t130 = 0;
        F.ret = t120;
        goto gm_fret;
        goto me15;
mn118:;
        uint8_t t132 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t132 == ((uint8_t)111ULL))) goto mn131;
        uint8_t t134 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_spi47lg4tr80j, 2) == 0));
        if (!t134) goto ie133;
        {
            gm_t9qkh0rsooc8d t136;
            GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)20ULL));
            gm_taari1s0m51a7 t137;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t137), t136);
            ((uint8_t *)&t137)[1] = 1;
            uint8_t t138 = 0;
            F.ret = t137;
            goto gm_fret;
        }
        goto ix133;
ie133:;
ix133:;
        goto me15;
mn131:;
        uint8_t t140 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t140 == ((uint8_t)112ULL))) goto mn139;
        uint8_t t142 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sjqs7q3ostqbm, 3) == 0));
        if (!t142) goto ie141;
        {
            gm_t9qkh0rsooc8d t144;
            GM_ST(uint8_t, ((uint8_t *)&t144 + 0), ((uint8_t)21ULL));
            gm_taari1s0m51a7 t145;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t145), t144);
            ((uint8_t *)&t145)[1] = 1;
            uint8_t t146 = 0;
            F.ret = t145;
            goto gm_fret;
        }
        goto ix141;
ie141:;
ix141:;
        goto me15;
mn139:;
        uint8_t t148 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t148 == ((uint8_t)114ULL))) goto mn147;
        gm_taari1s0m51a7 t149;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sn6sqlran7h3r, 6) == 0)) goto mn151;
        gm_t9qkh0rsooc8d t152;
        GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)22ULL));
        gm_taari1s0m51a7 t153;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t153), t152);
        ((uint8_t *)&t153)[1] = 1;
        t149 = t153;
        goto me150;
mn151:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s277jq8ulrihr, 6) == 0)) goto mn154;
        gm_t9qkh0rsooc8d t155;
        GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)23ULL));
        gm_taari1s0m51a7 t156;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t156), t155);
        ((uint8_t *)&t156)[1] = 1;
        t149 = t156;
        goto me150;
mn154:;
        gm_taari1s0m51a7 t158;
        memset(&t158, 0, sizeof t158);
        t149 = t158;
        goto me150;
mn157:;
        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 57, 23);
me150:;
        uint8_t t159 = 0;
        F.ret = t149;
        goto gm_fret;
        goto me15;
mn147:;
        uint8_t t161 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t161 == ((uint8_t)115ULL))) goto mn160;
        uint8_t t163 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sc49g68j2cekh, 6) == 0));
        if (!t163) goto ie162;
        {
            gm_t9qkh0rsooc8d t165;
            GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)24ULL));
            gm_taari1s0m51a7 t166;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t166), t165);
            ((uint8_t *)&t166)[1] = 1;
            uint8_t t167 = 0;
            F.ret = t166;
            goto gm_fret;
        }
        goto ix162;
ie162:;
ix162:;
        goto me15;
mn160:;
        uint8_t t169 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t169 == ((uint8_t)116ULL))) goto mn168;
        gm_taari1s0m51a7 t170;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sehko5bb96s6p, 4) == 0)) goto mn172;
        gm_t9qkh0rsooc8d t173;
        GM_ST(uint8_t, ((uint8_t *)&t173 + 0), ((uint8_t)25ULL));
        gm_taari1s0m51a7 t174;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t174), t173);
        ((uint8_t *)&t174)[1] = 1;
        t170 = t174;
        goto me171;
mn172:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s4fd35uidvgkn, 3) == 0)) goto mn175;
        gm_t9qkh0rsooc8d t176;
        GM_ST(uint8_t, ((uint8_t *)&t176 + 0), ((uint8_t)26ULL));
        gm_taari1s0m51a7 t177;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t177), t176);
        ((uint8_t *)&t177)[1] = 1;
        t170 = t177;
        goto me171;
mn175:;
        gm_taari1s0m51a7 t179;
        memset(&t179, 0, sizeof t179);
        t170 = t179;
        goto me171;
mn178:;
        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 63, 23);
me171:;
        uint8_t t180 = 0;
        F.ret = t170;
        goto gm_fret;
        goto me15;
mn168:;
        uint8_t t182 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t182 == ((uint8_t)117ULL))) goto mn181;
        gm_taari1s0m51a7 t183;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s8p1h96ji9kh0, 5) == 0)) goto mn185;
        gm_t9qkh0rsooc8d t186;
        GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)27ULL));
        gm_taari1s0m51a7 t187;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t187), t186);
        ((uint8_t *)&t187)[1] = 1;
        t183 = t187;
        goto me184;
mn185:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_smu48daue2qai, 5) == 0)) goto mn188;
        gm_t9qkh0rsooc8d t189;
        GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)28ULL));
        gm_taari1s0m51a7 t190;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t190), t189);
        ((uint8_t *)&t190)[1] = 1;
        t183 = t190;
        goto me184;
mn188:;
        gm_taari1s0m51a7 t192;
        memset(&t192, 0, sizeof t192);
        t183 = t192;
        goto me184;
mn191:;
        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 68, 23);
me184:;
        uint8_t t193 = 0;
        F.ret = t183;
        goto gm_fret;
        goto me15;
mn181:;
        uint8_t t195 = GM_LD(uint8_t, ((uint8_t *)&t14));
        if (!(t195 == ((uint8_t)119ULL))) goto mn194;
        gm_taari1s0m51a7 t196;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_slpo22sjs83b1, 4) == 0)) goto mn198;
        gm_t9qkh0rsooc8d t199;
        GM_ST(uint8_t, ((uint8_t *)&t199 + 0), ((uint8_t)29ULL));
        gm_taari1s0m51a7 t200;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t200), t199);
        ((uint8_t *)&t200)[1] = 1;
        t196 = t200;
        goto me197;
mn198:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s5rv6umk4akl0, 5) == 0)) goto mn201;
        gm_t9qkh0rsooc8d t202;
        GM_ST(uint8_t, ((uint8_t *)&t202 + 0), ((uint8_t)30ULL));
        gm_taari1s0m51a7 t203;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t203), t202);
        ((uint8_t *)&t203)[1] = 1;
        t196 = t203;
        goto me197;
mn201:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_srri8c6al1v1g, 4) == 0)) goto mn204;
        gm_t9qkh0rsooc8d t205;
        GM_ST(uint8_t, ((uint8_t *)&t205 + 0), ((uint8_t)31ULL));
        gm_taari1s0m51a7 t206;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t206), t205);
        ((uint8_t *)&t206)[1] = 1;
        t196 = t206;
        goto me197;
mn204:;
        gm_taari1s0m51a7 t208;
        memset(&t208, 0, sizeof t208);
        t196 = t208;
        goto me197;
mn207:;
        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 73, 23);
me197:;
        uint8_t t209 = 0;
        F.ret = t196;
        goto gm_fret;
        goto me15;
mn194:;
        goto me15;
mn210:;
        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 19, 5);
me15:;
    }
    {
        gm_taari1s0m51a7 t212;
        memset(&t212, 0, sizeof t212);
        uint8_t t213 = 0;
        F.ret = t212;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_keyword_text_149oqn4(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_keyword_text_149oqn4 F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 594u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t36) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma19; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma23; case ((uint8_t)21ULL): goto ma24; case ((uint8_t)22ULL): goto ma25; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma27; case ((uint8_t)25ULL): goto ma28; case ((uint8_t)26ULL): goto ma29; case ((uint8_t)27ULL): goto ma30; case ((uint8_t)28ULL): goto ma31; case ((uint8_t)29ULL): goto ma32; case ((uint8_t)30ULL): goto ma33; case ((uint8_t)31ULL): goto ma34; default: goto ma35; }
ma3:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t38 != ((uint8_t)0ULL)) goto mn37;
    gm_b16_8 t39;
    GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s920dpvvv6cej));
    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(3));
    t1 = t39;
    goto me2;
mn37:;
ma4:;
    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t41 != ((uint8_t)1ULL)) goto mn40;
    gm_b16_8 t42;
    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s8av3ru5o85v4));
    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(5));
    t1 = t42;
    goto me2;
mn40:;
ma5:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t44 != ((uint8_t)2ULL)) goto mn43;
    gm_b16_8 t45;
    GM_ST(void *, (uint8_t *)&t45, (void *)(gm_si1m7q6ia0hto));
    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(6));
    t1 = t45;
    goto me2;
mn43:;
ma6:;
    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t47 != ((uint8_t)3ULL)) goto mn46;
    gm_b16_8 t48;
    GM_ST(void *, (uint8_t *)&t48, (void *)(gm_sr31o7m0aohvd));
    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(8));
    t1 = t48;
    goto me2;
mn46:;
ma7:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t50 != ((uint8_t)4ULL)) goto mn49;
    gm_b16_8 t51;
    GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s66uvortgrtsg));
    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(3));
    t1 = t51;
    goto me2;
mn49:;
ma8:;
    uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t53 != ((uint8_t)5ULL)) goto mn52;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sllcm1pqagh28));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(5));
    t1 = t54;
    goto me2;
mn52:;
ma9:;
    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t56 != ((uint8_t)6ULL)) goto mn55;
    gm_b16_8 t57;
    GM_ST(void *, (uint8_t *)&t57, (void *)(gm_svqdrhms9i231));
    GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(6));
    t1 = t57;
    goto me2;
mn55:;
ma10:;
    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t59 != ((uint8_t)7ULL)) goto mn58;
    gm_b16_8 t60;
    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s9es2tos1bhfp));
    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(4));
    t1 = t60;
    goto me2;
mn58:;
ma11:;
    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t62 != ((uint8_t)8ULL)) goto mn61;
    gm_b16_8 t63;
    GM_ST(void *, (uint8_t *)&t63, (void *)(gm_ser8ip7vtbv6c));
    GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(8));
    t1 = t63;
    goto me2;
mn61:;
ma12:;
    uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t65 != ((uint8_t)9ULL)) goto mn64;
    gm_b16_8 t66;
    GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sb88rajuutaep));
    GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(3));
    t1 = t66;
    goto me2;
mn64:;
ma13:;
    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t68 != ((uint8_t)10ULL)) goto mn67;
    gm_b16_8 t69;
    GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sogehh8t1m7tn));
    GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(5));
    t1 = t69;
    goto me2;
mn67:;
ma14:;
    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t71 != ((uint8_t)11ULL)) goto mn70;
    gm_b16_8 t72;
    GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s579fb7l5l6df));
    GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(2));
    t1 = t72;
    goto me2;
mn70:;
ma15:;
    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t74 != ((uint8_t)12ULL)) goto mn73;
    gm_b16_8 t75;
    GM_ST(void *, (uint8_t *)&t75, (void *)(gm_scc8p0a9eachg));
    GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(3));
    t1 = t75;
    goto me2;
mn73:;
ma16:;
    uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t77 != ((uint8_t)13ULL)) goto mn76;
    gm_b16_8 t78;
    GM_ST(void *, (uint8_t *)&t78, (void *)(gm_sl9nvct67or45));
    GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(2));
    t1 = t78;
    goto me2;
mn76:;
ma17:;
    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t80 != ((uint8_t)14ULL)) goto mn79;
    gm_b16_8 t81;
    GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sg3kpa3h16no3));
    GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(6));
    t1 = t81;
    goto me2;
mn79:;
ma18:;
    uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t83 != ((uint8_t)15ULL)) goto mn82;
    gm_b16_8 t84;
    GM_ST(void *, (uint8_t *)&t84, (void *)(gm_svbhcvlahd4n2));
    GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(2));
    t1 = t84;
    goto me2;
mn82:;
ma19:;
    uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t86 != ((uint8_t)16ULL)) goto mn85;
    gm_b16_8 t87;
    GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sgpdmvc9s0d2p));
    GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(2));
    t1 = t87;
    goto me2;
mn85:;
ma20:;
    uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t89 != ((uint8_t)17ULL)) goto mn88;
    gm_b16_8 t90;
    GM_ST(void *, (uint8_t *)&t90, (void *)(gm_s8m5q4i8uoe5j));
    GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(5));
    t1 = t90;
    goto me2;
mn88:;
ma21:;
    uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t92 != ((uint8_t)18ULL)) goto mn91;
    gm_b16_8 t93;
    GM_ST(void *, (uint8_t *)&t93, (void *)(gm_scvhpklutvpjg));
    GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(3));
    t1 = t93;
    goto me2;
mn91:;
ma22:;
    uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t95 != ((uint8_t)19ULL)) goto mn94;
    gm_b16_8 t96;
    GM_ST(void *, (uint8_t *)&t96, (void *)(gm_squedv51820an));
    GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(4));
    t1 = t96;
    goto me2;
mn94:;
ma23:;
    uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t98 != ((uint8_t)20ULL)) goto mn97;
    gm_b16_8 t99;
    GM_ST(void *, (uint8_t *)&t99, (void *)(gm_spi47lg4tr80j));
    GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(2));
    t1 = t99;
    goto me2;
mn97:;
ma24:;
    uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t101 != ((uint8_t)21ULL)) goto mn100;
    gm_b16_8 t102;
    GM_ST(void *, (uint8_t *)&t102, (void *)(gm_sjqs7q3ostqbm));
    GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(3));
    t1 = t102;
    goto me2;
mn100:;
ma25:;
    uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t104 != ((uint8_t)22ULL)) goto mn103;
    gm_b16_8 t105;
    GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sn6sqlran7h3r));
    GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(6));
    t1 = t105;
    goto me2;
mn103:;
ma26:;
    uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t107 != ((uint8_t)23ULL)) goto mn106;
    gm_b16_8 t108;
    GM_ST(void *, (uint8_t *)&t108, (void *)(gm_s277jq8ulrihr));
    GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(6));
    t1 = t108;
    goto me2;
mn106:;
ma27:;
    uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t110 != ((uint8_t)24ULL)) goto mn109;
    gm_b16_8 t111;
    GM_ST(void *, (uint8_t *)&t111, (void *)(gm_sc49g68j2cekh));
    GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(6));
    t1 = t111;
    goto me2;
mn109:;
ma28:;
    uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t113 != ((uint8_t)25ULL)) goto mn112;
    gm_b16_8 t114;
    GM_ST(void *, (uint8_t *)&t114, (void *)(gm_sehko5bb96s6p));
    GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(4));
    t1 = t114;
    goto me2;
mn112:;
ma29:;
    uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t116 != ((uint8_t)26ULL)) goto mn115;
    gm_b16_8 t117;
    GM_ST(void *, (uint8_t *)&t117, (void *)(gm_s4fd35uidvgkn));
    GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(3));
    t1 = t117;
    goto me2;
mn115:;
ma30:;
    uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t119 != ((uint8_t)27ULL)) goto mn118;
    gm_b16_8 t120;
    GM_ST(void *, (uint8_t *)&t120, (void *)(gm_s8p1h96ji9kh0));
    GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(5));
    t1 = t120;
    goto me2;
mn118:;
ma31:;
    uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t122 != ((uint8_t)28ULL)) goto mn121;
    gm_b16_8 t123;
    GM_ST(void *, (uint8_t *)&t123, (void *)(gm_smu48daue2qai));
    GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(5));
    t1 = t123;
    goto me2;
mn121:;
ma32:;
    uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t125 != ((uint8_t)29ULL)) goto mn124;
    gm_b16_8 t126;
    GM_ST(void *, (uint8_t *)&t126, (void *)(gm_slpo22sjs83b1));
    GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(4));
    t1 = t126;
    goto me2;
mn124:;
ma33:;
    uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t128 != ((uint8_t)30ULL)) goto mn127;
    gm_b16_8 t129;
    GM_ST(void *, (uint8_t *)&t129, (void *)(gm_s5rv6umk4akl0));
    GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(5));
    t1 = t129;
    goto me2;
mn127:;
ma34:;
    uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t131 != ((uint8_t)31ULL)) goto mn130;
    gm_b16_8 t132;
    GM_ST(void *, (uint8_t *)&t132, (void *)(gm_srri8c6al1v1g));
    GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(4));
    t1 = t132;
    goto me2;
mn130:;
ma35:;
    gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 84, 49);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_punct_text_ov4r40u(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_punct_text_ov4r40u F;
    gm_t9qkh0rsooc8d gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 1223u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t73) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma19; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma23; case ((uint8_t)21ULL): goto ma24; case ((uint8_t)22ULL): goto ma25; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma27; case ((uint8_t)25ULL): goto ma28; case ((uint8_t)26ULL): goto ma29; case ((uint8_t)27ULL): goto ma30; case ((uint8_t)28ULL): goto ma31; case ((uint8_t)29ULL): goto ma32; case ((uint8_t)30ULL): goto ma33; case ((uint8_t)31ULL): goto ma34; case ((uint8_t)32ULL): goto ma35; case ((uint8_t)33ULL): goto ma36; case ((uint8_t)34ULL): goto ma37; case ((uint8_t)35ULL): goto ma38; case ((uint8_t)36ULL): goto ma39; case ((uint8_t)37ULL): goto ma40; case ((uint8_t)38ULL): goto ma41; case ((uint8_t)39ULL): goto ma42; case ((uint8_t)40ULL): goto ma43; case ((uint8_t)41ULL): goto ma44; case ((uint8_t)42ULL): goto ma45; case ((uint8_t)43ULL): goto ma46; case ((uint8_t)44ULL): goto ma47; case ((uint8_t)45ULL): goto ma48; case ((uint8_t)46ULL): goto ma49; case ((uint8_t)47ULL): goto ma50; case ((uint8_t)48ULL): goto ma51; case ((uint8_t)49ULL): goto ma52; case ((uint8_t)50ULL): goto ma53; case ((uint8_t)51ULL): goto ma54; case ((uint8_t)52ULL): goto ma55; case ((uint8_t)53ULL): goto ma56; case ((uint8_t)54ULL): goto ma57; case ((uint8_t)55ULL): goto ma58; case ((uint8_t)56ULL): goto ma59; case ((uint8_t)57ULL): goto ma60; case ((uint8_t)58ULL): goto ma61; case ((uint8_t)59ULL): goto ma62; case ((uint8_t)60ULL): goto ma63; case ((uint8_t)61ULL): goto ma64; case ((uint8_t)62ULL): goto ma65; case ((uint8_t)63ULL): goto ma66; case ((uint8_t)64ULL): goto ma67; case ((uint8_t)65ULL): goto ma68; case ((uint8_t)66ULL): goto ma69; case ((uint8_t)67ULL): goto ma70; case ((uint8_t)68ULL): goto ma71; default: goto ma72; }
ma3:;
    uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t75 != ((uint8_t)0ULL)) goto mn74;
    gm_b16_8 t76;
    GM_ST(void *, (uint8_t *)&t76, (void *)(gm_sajhum9b4nr96));
    GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(1));
    t1 = t76;
    goto me2;
mn74:;
ma4:;
    uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t78 != ((uint8_t)1ULL)) goto mn77;
    gm_b16_8 t79;
    GM_ST(void *, (uint8_t *)&t79, (void *)(gm_skolmnfc0ccbd));
    GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(1));
    t1 = t79;
    goto me2;
mn77:;
ma5:;
    uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t81 != ((uint8_t)2ULL)) goto mn80;
    gm_b16_8 t82;
    GM_ST(void *, (uint8_t *)&t82, (void *)(gm_s6s96daqmhcs9));
    GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(1));
    t1 = t82;
    goto me2;
mn80:;
ma6:;
    uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t84 != ((uint8_t)3ULL)) goto mn83;
    gm_b16_8 t85;
    GM_ST(void *, (uint8_t *)&t85, (void *)(gm_st3ja0nqi37bv));
    GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(1));
    t1 = t85;
    goto me2;
mn83:;
ma7:;
    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t87 != ((uint8_t)4ULL)) goto mn86;
    gm_b16_8 t88;
    GM_ST(void *, (uint8_t *)&t88, (void *)(gm_siamquc19uikr));
    GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(1));
    t1 = t88;
    goto me2;
mn86:;
ma8:;
    uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t90 != ((uint8_t)5ULL)) goto mn89;
    gm_b16_8 t91;
    GM_ST(void *, (uint8_t *)&t91, (void *)(gm_si62ejtp0vbct));
    GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(2));
    t1 = t91;
    goto me2;
mn89:;
ma9:;
    uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t93 != ((uint8_t)6ULL)) goto mn92;
    gm_b16_8 t94;
    GM_ST(void *, (uint8_t *)&t94, (void *)(gm_s9hi985r6f61m));
    GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(2));
    t1 = t94;
    goto me2;
mn92:;
ma10:;
    uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t96 != ((uint8_t)7ULL)) goto mn95;
    gm_b16_8 t97;
    GM_ST(void *, (uint8_t *)&t97, (void *)(gm_smerqm8g793l6));
    GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(1));
    t1 = t97;
    goto me2;
mn95:;
ma11:;
    uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t99 != ((uint8_t)8ULL)) goto mn98;
    gm_b16_8 t100;
    GM_ST(void *, (uint8_t *)&t100, (void *)(gm_s2u5posfkbgnq));
    GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(1));
    t1 = t100;
    goto me2;
mn98:;
ma12:;
    uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t102 != ((uint8_t)9ULL)) goto mn101;
    gm_b16_8 t103;
    GM_ST(void *, (uint8_t *)&t103, (void *)(gm_sa831sfid0hf3));
    GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(1));
    t1 = t103;
    goto me2;
mn101:;
ma13:;
    uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t105 != ((uint8_t)10ULL)) goto mn104;
    gm_b16_8 t106;
    GM_ST(void *, (uint8_t *)&t106, (void *)(gm_skriisqpofdin));
    GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(2));
    t1 = t106;
    goto me2;
mn104:;
ma14:;
    uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t108 != ((uint8_t)11ULL)) goto mn107;
    gm_b16_8 t109;
    GM_ST(void *, (uint8_t *)&t109, (void *)(gm_sjjig60ja37ka));
    GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(2));
    t1 = t109;
    goto me2;
mn107:;
ma15:;
    uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t111 != ((uint8_t)12ULL)) goto mn110;
    gm_b16_8 t112;
    GM_ST(void *, (uint8_t *)&t112, (void *)(gm_s1dbslfck7laf));
    GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(2));
    t1 = t112;
    goto me2;
mn110:;
ma16:;
    uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t114 != ((uint8_t)13ULL)) goto mn113;
    gm_b16_8 t115;
    GM_ST(void *, (uint8_t *)&t115, (void *)(gm_sgemh066sqogf));
    GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(3));
    t1 = t115;
    goto me2;
mn113:;
ma17:;
    uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t117 != ((uint8_t)14ULL)) goto mn116;
    gm_b16_8 t118;
    GM_ST(void *, (uint8_t *)&t118, (void *)(gm_s0s4ef50964r9));
    GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(2));
    t1 = t118;
    goto me2;
mn116:;
ma18:;
    uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t120 != ((uint8_t)15ULL)) goto mn119;
    gm_b16_8 t121;
    GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sfg1e0lbt8sf5));
    GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(2));
    t1 = t121;
    goto me2;
mn119:;
ma19:;
    uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t123 != ((uint8_t)16ULL)) goto mn122;
    gm_b16_8 t124;
    GM_ST(void *, (uint8_t *)&t124, (void *)(gm_sad0udrpgk27o));
    GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(2));
    t1 = t124;
    goto me2;
mn122:;
ma20:;
    uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t126 != ((uint8_t)17ULL)) goto mn125;
    gm_b16_8 t127;
    GM_ST(void *, (uint8_t *)&t127, (void *)(gm_s9s0gbnmtpvjl));
    GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(3));
    t1 = t127;
    goto me2;
mn125:;
ma21:;
    uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t129 != ((uint8_t)18ULL)) goto mn128;
    gm_b16_8 t130;
    GM_ST(void *, (uint8_t *)&t130, (void *)(gm_sjid61i849qic));
    GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(2));
    t1 = t130;
    goto me2;
mn128:;
ma22:;
    uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t132 != ((uint8_t)19ULL)) goto mn131;
    gm_b16_8 t133;
    GM_ST(void *, (uint8_t *)&t133, (void *)(gm_sabc7m3rug04j));
    GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(2));
    t1 = t133;
    goto me2;
mn131:;
ma23:;
    uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t135 != ((uint8_t)20ULL)) goto mn134;
    gm_b16_8 t136;
    GM_ST(void *, (uint8_t *)&t136, (void *)(gm_s3bkmscn5fi56));
    GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(2));
    t1 = t136;
    goto me2;
mn134:;
ma24:;
    uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t138 != ((uint8_t)21ULL)) goto mn137;
    gm_b16_8 t139;
    GM_ST(void *, (uint8_t *)&t139, (void *)(gm_sp3rdossibc7f));
    GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(3));
    t1 = t139;
    goto me2;
mn137:;
ma25:;
    uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t141 != ((uint8_t)22ULL)) goto mn140;
    gm_b16_8 t142;
    GM_ST(void *, (uint8_t *)&t142, (void *)(gm_sh0j2ggs1vvoa));
    GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)(1));
    t1 = t142;
    goto me2;
mn140:;
ma26:;
    uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t144 != ((uint8_t)23ULL)) goto mn143;
    gm_b16_8 t145;
    GM_ST(void *, (uint8_t *)&t145, (void *)(gm_sdeq555foclko));
    GM_ST(uintptr_t, (uint8_t *)&t145 + 8, (uintptr_t)(2));
    t1 = t145;
    goto me2;
mn143:;
ma27:;
    uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t147 != ((uint8_t)24ULL)) goto mn146;
    gm_b16_8 t148;
    GM_ST(void *, (uint8_t *)&t148, (void *)(gm_shtiokhdiebr1));
    GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(2));
    t1 = t148;
    goto me2;
mn146:;
ma28:;
    uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t150 != ((uint8_t)25ULL)) goto mn149;
    gm_b16_8 t151;
    GM_ST(void *, (uint8_t *)&t151, (void *)(gm_sd7p31mtitiq9));
    GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(2));
    t1 = t151;
    goto me2;
mn149:;
ma29:;
    uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t153 != ((uint8_t)26ULL)) goto mn152;
    gm_b16_8 t154;
    GM_ST(void *, (uint8_t *)&t154, (void *)(gm_sj0kptrfbec99));
    GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(2));
    t1 = t154;
    goto me2;
mn152:;
ma30:;
    uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t156 != ((uint8_t)27ULL)) goto mn155;
    gm_b16_8 t157;
    GM_ST(void *, (uint8_t *)&t157, (void *)(gm_sj5j224m4ulf9));
    GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(2));
    t1 = t157;
    goto me2;
mn155:;
ma31:;
    uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t159 != ((uint8_t)28ULL)) goto mn158;
    gm_b16_8 t160;
    GM_ST(void *, (uint8_t *)&t160, (void *)(gm_sbjiv038rtlcj));
    GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(3));
    t1 = t160;
    goto me2;
mn158:;
ma32:;
    uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t162 != ((uint8_t)29ULL)) goto mn161;
    gm_b16_8 t163;
    GM_ST(void *, (uint8_t *)&t163, (void *)(gm_su9718bmbci33));
    GM_ST(uintptr_t, (uint8_t *)&t163 + 8, (uintptr_t)(3));
    t1 = t163;
    goto me2;
mn161:;
ma33:;
    uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t165 != ((uint8_t)30ULL)) goto mn164;
    gm_b16_8 t166;
    GM_ST(void *, (uint8_t *)&t166, (void *)(gm_srcsoo8lineh7));
    GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(2));
    t1 = t166;
    goto me2;
mn164:;
ma34:;
    uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t168 != ((uint8_t)31ULL)) goto mn167;
    gm_b16_8 t169;
    GM_ST(void *, (uint8_t *)&t169, (void *)(gm_saimepa8bfkkm));
    GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(2));
    t1 = t169;
    goto me2;
mn167:;
ma35:;
    uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t171 != ((uint8_t)32ULL)) goto mn170;
    gm_b16_8 t172;
    GM_ST(void *, (uint8_t *)&t172, (void *)(gm_sl4co37h2cmmb));
    GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(2));
    t1 = t172;
    goto me2;
mn170:;
ma36:;
    uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t174 != ((uint8_t)33ULL)) goto mn173;
    gm_b16_8 t175;
    GM_ST(void *, (uint8_t *)&t175, (void *)(gm_sdavnmflj0bfl));
    GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(3));
    t1 = t175;
    goto me2;
mn173:;
ma37:;
    uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t177 != ((uint8_t)34ULL)) goto mn176;
    gm_b16_8 t178;
    GM_ST(void *, (uint8_t *)&t178, (void *)(gm_sll7gknpe999d));
    GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(3));
    t1 = t178;
    goto me2;
mn176:;
ma38:;
    uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t180 != ((uint8_t)35ULL)) goto mn179;
    gm_b16_8 t181;
    GM_ST(void *, (uint8_t *)&t181, (void *)(gm_sq1oij2hrovtv));
    GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(3));
    t1 = t181;
    goto me2;
mn179:;
ma39:;
    uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t183 != ((uint8_t)36ULL)) goto mn182;
    gm_b16_8 t184;
    GM_ST(void *, (uint8_t *)&t184, (void *)(gm_sm3gke457vdm3));
    GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(4));
    t1 = t184;
    goto me2;
mn182:;
ma40:;
    uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t186 != ((uint8_t)37ULL)) goto mn185;
    gm_b16_8 t187;
    GM_ST(void *, (uint8_t *)&t187, (void *)(gm_sde2pekt9oc5v));
    GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(3));
    t1 = t187;
    goto me2;
mn185:;
ma41:;
    uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t189 != ((uint8_t)38ULL)) goto mn188;
    gm_b16_8 t190;
    GM_ST(void *, (uint8_t *)&t190, (void *)(gm_skm1g73974f87));
    GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(3));
    t1 = t190;
    goto me2;
mn188:;
ma42:;
    uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t192 != ((uint8_t)39ULL)) goto mn191;
    gm_b16_8 t193;
    GM_ST(void *, (uint8_t *)&t193, (void *)(gm_svkleo8hhv87n));
    GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(3));
    t1 = t193;
    goto me2;
mn191:;
ma43:;
    uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t195 != ((uint8_t)40ULL)) goto mn194;
    gm_b16_8 t196;
    GM_ST(void *, (uint8_t *)&t196, (void *)(gm_s33pl04oikskr));
    GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(4));
    t1 = t196;
    goto me2;
mn194:;
ma44:;
    uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t198 != ((uint8_t)41ULL)) goto mn197;
    gm_b16_8 t199;
    GM_ST(void *, (uint8_t *)&t199, (void *)(gm_s1f440st4mnkp));
    GM_ST(uintptr_t, (uint8_t *)&t199 + 8, (uintptr_t)(1));
    t1 = t199;
    goto me2;
mn197:;
ma45:;
    uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t201 != ((uint8_t)42ULL)) goto mn200;
    gm_b16_8 t202;
    GM_ST(void *, (uint8_t *)&t202, (void *)(gm_s4g3cajt15dmi));
    GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(2));
    t1 = t202;
    goto me2;
mn200:;
ma46:;
    uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t204 != ((uint8_t)43ULL)) goto mn203;
    gm_b16_8 t205;
    GM_ST(void *, (uint8_t *)&t205, (void *)(gm_sm5be4659ulci));
    GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(1));
    t1 = t205;
    goto me2;
mn203:;
ma47:;
    uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t207 != ((uint8_t)44ULL)) goto mn206;
    gm_b16_8 t208;
    GM_ST(void *, (uint8_t *)&t208, (void *)(gm_s02ru83e3fffc));
    GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(2));
    t1 = t208;
    goto me2;
mn206:;
ma48:;
    uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t210 != ((uint8_t)45ULL)) goto mn209;
    gm_b16_8 t211;
    GM_ST(void *, (uint8_t *)&t211, (void *)(gm_sfqk3rro4g7t0));
    GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(1));
    t1 = t211;
    goto me2;
mn209:;
ma49:;
    uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t213 != ((uint8_t)46ULL)) goto mn212;
    gm_b16_8 t214;
    GM_ST(void *, (uint8_t *)&t214, (void *)(gm_sf9a2kg3eo1gs));
    GM_ST(uintptr_t, (uint8_t *)&t214 + 8, (uintptr_t)(1));
    t1 = t214;
    goto me2;
mn212:;
ma50:;
    uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t216 != ((uint8_t)47ULL)) goto mn215;
    gm_b16_8 t217;
    GM_ST(void *, (uint8_t *)&t217, (void *)(gm_srs1vrn2uaams));
    GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(1));
    t1 = t217;
    goto me2;
mn215:;
ma51:;
    uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t219 != ((uint8_t)48ULL)) goto mn218;
    gm_b16_8 t220;
    GM_ST(void *, (uint8_t *)&t220, (void *)(gm_sfi2ha42r1nr2));
    GM_ST(uintptr_t, (uint8_t *)&t220 + 8, (uintptr_t)(1));
    t1 = t220;
    goto me2;
mn218:;
ma52:;
    uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t222 != ((uint8_t)49ULL)) goto mn221;
    gm_b16_8 t223;
    GM_ST(void *, (uint8_t *)&t223, (void *)(gm_seccpktgmrevt));
    GM_ST(uintptr_t, (uint8_t *)&t223 + 8, (uintptr_t)(1));
    t1 = t223;
    goto me2;
mn221:;
ma53:;
    uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t225 != ((uint8_t)50ULL)) goto mn224;
    gm_b16_8 t226;
    GM_ST(void *, (uint8_t *)&t226, (void *)(gm_snenkhfev166s));
    GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(2));
    t1 = t226;
    goto me2;
mn224:;
ma54:;
    uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t228 != ((uint8_t)51ULL)) goto mn227;
    gm_b16_8 t229;
    GM_ST(void *, (uint8_t *)&t229, (void *)(gm_svm0rfaqk3b2q));
    GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(3));
    t1 = t229;
    goto me2;
mn227:;
ma55:;
    uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t231 != ((uint8_t)52ULL)) goto mn230;
    gm_b16_8 t232;
    GM_ST(void *, (uint8_t *)&t232, (void *)(gm_soqt3jnmovq5l));
    GM_ST(uintptr_t, (uint8_t *)&t232 + 8, (uintptr_t)(3));
    t1 = t232;
    goto me2;
mn230:;
ma56:;
    uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t234 != ((uint8_t)53ULL)) goto mn233;
    gm_b16_8 t235;
    GM_ST(void *, (uint8_t *)&t235, (void *)(gm_sjfg2291kjugh));
    GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(3));
    t1 = t235;
    goto me2;
mn233:;
ma57:;
    uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t237 != ((uint8_t)54ULL)) goto mn236;
    gm_b16_8 t238;
    GM_ST(void *, (uint8_t *)&t238, (void *)(gm_ss67f0gmfen1m));
    GM_ST(uintptr_t, (uint8_t *)&t238 + 8, (uintptr_t)(2));
    t1 = t238;
    goto me2;
mn236:;
ma58:;
    uint8_t t240 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t240 != ((uint8_t)55ULL)) goto mn239;
    gm_b16_8 t241;
    GM_ST(void *, (uint8_t *)&t241, (void *)(gm_shge5n743ua4h));
    GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(2));
    t1 = t241;
    goto me2;
mn239:;
ma59:;
    uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t243 != ((uint8_t)56ULL)) goto mn242;
    gm_b16_8 t244;
    GM_ST(void *, (uint8_t *)&t244, (void *)(gm_s39mljh669qgl));
    GM_ST(uintptr_t, (uint8_t *)&t244 + 8, (uintptr_t)(1));
    t1 = t244;
    goto me2;
mn242:;
ma60:;
    uint8_t t246 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t246 != ((uint8_t)57ULL)) goto mn245;
    gm_b16_8 t247;
    GM_ST(void *, (uint8_t *)&t247, (void *)(gm_sc8o4oaph9gbr));
    GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(1));
    t1 = t247;
    goto me2;
mn245:;
ma61:;
    uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t249 != ((uint8_t)58ULL)) goto mn248;
    gm_b16_8 t250;
    GM_ST(void *, (uint8_t *)&t250, (void *)(gm_sl3q52ep6aa53));
    GM_ST(uintptr_t, (uint8_t *)&t250 + 8, (uintptr_t)(1));
    t1 = t250;
    goto me2;
mn248:;
ma62:;
    uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t252 != ((uint8_t)59ULL)) goto mn251;
    gm_b16_8 t253;
    GM_ST(void *, (uint8_t *)&t253, (void *)(gm_sm8obe455p2d9));
    GM_ST(uintptr_t, (uint8_t *)&t253 + 8, (uintptr_t)(2));
    t1 = t253;
    goto me2;
mn251:;
ma63:;
    uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t255 != ((uint8_t)60ULL)) goto mn254;
    gm_b16_8 t256;
    GM_ST(void *, (uint8_t *)&t256, (void *)(gm_s0eemp7of8buc));
    GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(2));
    t1 = t256;
    goto me2;
mn254:;
ma64:;
    uint8_t t258 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t258 != ((uint8_t)61ULL)) goto mn257;
    gm_b16_8 t259;
    GM_ST(void *, (uint8_t *)&t259, (void *)(gm_smooaouqmm8dj));
    GM_ST(uintptr_t, (uint8_t *)&t259 + 8, (uintptr_t)(1));
    t1 = t259;
    goto me2;
mn257:;
ma65:;
    uint8_t t261 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t261 != ((uint8_t)62ULL)) goto mn260;
    gm_b16_8 t262;
    GM_ST(void *, (uint8_t *)&t262, (void *)(gm_s1asl1e1tvb0k));
    GM_ST(uintptr_t, (uint8_t *)&t262 + 8, (uintptr_t)(1));
    t1 = t262;
    goto me2;
mn260:;
ma66:;
    uint8_t t264 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t264 != ((uint8_t)63ULL)) goto mn263;
    gm_b16_8 t265;
    GM_ST(void *, (uint8_t *)&t265, (void *)(gm_sk754mj3p1el6));
    GM_ST(uintptr_t, (uint8_t *)&t265 + 8, (uintptr_t)(1));
    t1 = t265;
    goto me2;
mn263:;
ma67:;
    uint8_t t267 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t267 != ((uint8_t)64ULL)) goto mn266;
    gm_b16_8 t268;
    GM_ST(void *, (uint8_t *)&t268, (void *)(gm_s2053hhvf16fm));
    GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(1));
    t1 = t268;
    goto me2;
mn266:;
ma68:;
    uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t270 != ((uint8_t)65ULL)) goto mn269;
    gm_b16_8 t271;
    GM_ST(void *, (uint8_t *)&t271, (void *)(gm_sh40na9qbga3l));
    GM_ST(uintptr_t, (uint8_t *)&t271 + 8, (uintptr_t)(1));
    t1 = t271;
    goto me2;
mn269:;
ma69:;
    uint8_t t273 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t273 != ((uint8_t)66ULL)) goto mn272;
    gm_b16_8 t274;
    GM_ST(void *, (uint8_t *)&t274, (void *)(gm_s2cuflhqkkuaq));
    GM_ST(uintptr_t, (uint8_t *)&t274 + 8, (uintptr_t)(1));
    t1 = t274;
    goto me2;
mn272:;
ma70:;
    uint8_t t276 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t276 != ((uint8_t)67ULL)) goto mn275;
    gm_b16_8 t277;
    GM_ST(void *, (uint8_t *)&t277, (void *)(gm_s1c4sfff8ac39));
    GM_ST(uintptr_t, (uint8_t *)&t277 + 8, (uintptr_t)(6));
    t1 = t277;
    goto me2;
mn275:;
ma71:;
    uint8_t t279 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t279 != ((uint8_t)68ULL)) goto mn278;
    gm_b16_8 t280;
    GM_ST(void *, (uint8_t *)&t280, (void *)(gm_sidtn67sdk4e0));
    GM_ST(uintptr_t, (uint8_t *)&t280 + 8, (uintptr_t)(6));
    t1 = t280;
    goto me2;
mn278:;
ma72:;
    gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 146, 45);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
