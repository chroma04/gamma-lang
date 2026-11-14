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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
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
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
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
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
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
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { uint64_t f0; gm_th2lqbo77pvsc f1; } gm_t13urt52695sh;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t46lmulm8gaj3) == 12 && _Alignof(gm_t46lmulm8gaj3) == 4 && offsetof(gm_t46lmulm8gaj3, f0) == 0 && offsetof(gm_t46lmulm8gaj3, f1) == 8, "layout of `?IntSuffix`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
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
typedef struct { uint64_t f0; gm_t5v0c8ia6a5mo f1; } gm_tgsvmddj3e85e;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_th2lqbo77pvsc f0; void * f1; } gm_tpouv9r7lrri2;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tbgd14esf0jvi v1; } f1; } gm_ts5t31j0bugq1;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t13urt52695sh) == 32 && _Alignof(gm_t13urt52695sh) == 8 && offsetof(gm_t13urt52695sh, f0) == 0 && offsetof(gm_t13urt52695sh, f1) == 8, "layout of `(i: usize, x: ?string)`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { gm_t1s75i08qtptd f0; } gm_t4vsf0g4hsv81;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_t46lmulm8gaj3 f1; uint8_t f2; uint8_t pe[3]; } gm_ta42iqot1auae;
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_ts5t31j0bugq1 f0; uint8_t f1; uint8_t pe[7]; } gm_tck5nas0nf5v8;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tgsvmddj3e85e) == 64 && _Alignof(gm_tgsvmddj3e85e) == 8 && offsetof(gm_tgsvmddj3e85e, f0) == 0 && offsetof(gm_tgsvmddj3e85e, f1) == 8, "layout of `(i: usize, x: ParamInfo)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpouv9r7lrri2) == 32 && _Alignof(gm_tpouv9r7lrri2) == 8 && offsetof(gm_tpouv9r7lrri2, f0) == 0 && offsetof(gm_tpouv9r7lrri2, f1) == 24, "layout of `SpreadPart`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts5t31j0bugq1) == 40 && _Alignof(gm_ts5t31j0bugq1) == 8 && offsetof(gm_ts5t31j0bugq1, f0) == 0 && offsetof(gm_ts5t31j0bugq1, f1) == 8, "layout of `Packed`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { gm_t4vsf0g4hsv81 f0; uint8_t f1; uint8_t pe[7]; } gm_t13p2dc04q01f;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t4vsf0g4hsv81) == 40 && _Alignof(gm_t4vsf0g4hsv81) == 8 && offsetof(gm_t4vsf0g4hsv81, f0) == 0, "layout of `(name: ?Ident)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_ta42iqot1auae) == 64 && _Alignof(gm_ta42iqot1auae) == 8 && offsetof(gm_ta42iqot1auae, f0) == 0 && offsetof(gm_ta42iqot1auae, f1) == 48 && offsetof(gm_ta42iqot1auae, f2) == 60, "layout of `IntLit`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tck5nas0nf5v8) == 48 && _Alignof(gm_tck5nas0nf5v8) == 8 && offsetof(gm_tck5nas0nf5v8, f0) == 0 && offsetof(gm_tck5nas0nf5v8, f1) == 40, "layout of `?Packed`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t13p2dc04q01f) == 48 && _Alignof(gm_t13p2dc04q01f) == 8 && offsetof(gm_t13p2dc04q01f, f0) == 0 && offsetof(gm_t13p2dc04q01f, f1) == 40, "layout of `?(name: ?Ident)`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
typedef struct { uint64_t f0; gm_t7te2tdpebbt4 f1; } gm_tl1vi0nd1uql9;
_Static_assert(sizeof(gm_tl1vi0nd1uql9) == 152 && _Alignof(gm_tl1vi0nd1uql9) == 8 && offsetof(gm_tl1vi0nd1uql9, f0) == 0 && offsetof(gm_tl1vi0nd1uql9, f1) == 8, "layout of `(i: usize, x: Arg)`");
struct gm_fr_array_element_t6q5jtp {
    gm_frame h;
    void * ret;
};
struct gm_fr_elements_tuple_gdm5n6q {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* stmts */
    gm_trqfvmjtt4sij l8; /* elems */
    gm_trqfvmjtt4sij l9; /* ts */
    void * ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8ckfo47 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8lf24cm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fa4sjvq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ppl3r45 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_0c2v515 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6oos54v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fvvkq82 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_j5ps4qc {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_pack_rest_args_cknfrfl {
    gm_frame h;
    gm_t5v0c8ia6a5mo l8; /* p */
    gm_trqfvmjtt4sij l14; /* out */
    gm_trqfvmjtt4sij l15; /* rest */
    gm_t7te2tdpebbt4 l20; /* a */
    gm_t7te2tdpebbt4 l22; /* packed */
    gm_tc3oht4ksth9d l23; /* e */
    gm_trqfvmjtt4sij l25; /* elems */
    gm_tc3oht4ksth9d l29; /* e */
    gm_tck5nas0nf5v8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bsig5q3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d0271rf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_t69m56meqefs8 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_jis5lhu {
    gm_frame h;
    gm_t7te2tdpebbt4 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l8mrumu {
    gm_frame h;
    gm_tv1o1j44ii4db l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_08uc6h5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_47d6kh3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_e5frfkp {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_nl0tf27 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_fsnl6fa {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_spread_array_v7s332a {
    gm_frame h;
    gm_tb7u19loo3j87 ret;
};
struct gm_fr_spread_generic_args_8qtof2f {
    gm_frame h;
    gm_t7te2tdpebbt4 l8; /* a */
    gm_trqfvmjtt4sij l9; /* written */
    gm_t7te2tdpebbt4 l13; /* a */
    gm_tc3oht4ksth9d l14; /* x */
    gm_tc3oht4ksth9d l15; /* x */
    gm_tc3oht4ksth9d l16; /* x */
    gm_tc3oht4ksth9d l17; /* e */
    gm_t13p2dc04q01f l18; /* named */
    gm_tc3oht4ksth9d l24; /* base */
    gm_t4vsf0g4hsv81 l25; /* nm */
    gm_trqfvmjtt4sij l27; /* parts */
    gm_ta42iqot1auae l37; /* lit */
    gm_tc3oht4ksth9d l39; /* elem */
    gm_t0mglksrkfa5e l44; /* x */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_spread_parts_e62q2l2 {
    gm_frame h;
    gm_trqfvmjtt4sij l12; /* parts */
    gm_trqfvmjtt4sij l18; /* ts */
    gm_trqfvmjtt4sij l19; /* ns */
    gm_t0mglksrkfa5e l22; /* x */
    gm_trqfvmjtt4sij l24; /* parts */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_synthetic_hdhcgpb {
    gm_frame h;
    gm_t2gtbh39ptekd l0; /* kind */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_array_element_t6q5jtp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_elements_tuple_gdm5n6q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8ckfo47(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8lf24cm(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) void gm_f_format_7f8ekea(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_l53b7vj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_p5cs5qn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_q62qvnl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_uubl3s3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void gm_f_hidden_field_u7uao2s(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_insert_3qmicjr(void * a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fvvkq82(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_j5ps4qc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_q1fs5ps(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_pack_rest_args_cknfrfl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bsig5q3(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d0271rf(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l8mrumu(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_08uc6h5(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_47d6kh3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_e5frfkp(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_nl0tf27(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_spread_array_v7s332a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_spread_generic_args_8qtof2f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) void gm_f_spread_parts_e62q2l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_synthetic_hdhcgpb(void *gm_out, const void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tc3oht4ksth9d gm_f_synthetic_hdhcgpb_c(gm_t2gtbh39ptekd a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k70n01rs29g[40] __attribute__((aligned(8)));
extern uint8_t gm_ka2mtmecqk3[40] __attribute__((aligned(8)));
extern uint8_t gm_kbjtngq55qv[40] __attribute__((aligned(8)));
extern uint8_t gm_ke30i57o09o[40] __attribute__((aligned(8)));
extern uint8_t gm_kjn96jdce98[40] __attribute__((aligned(8)));
extern uint8_t gm_kqn03v7t2qt[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1olfuo31ml9r[64];
extern const uint8_t gm_s2of8ntot1on0[81];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcdjp1iiokf034[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8ckfo47(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8ckfo47 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 136)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8lf24cm(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8lf24cm F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fa4sjvq F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 144)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ijtp9lo F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)1ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 1)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ppl3r45 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 4)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_89glhjr F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        gm_tl874t2atnlh3 t5 = gm_l1;
        gm_b16_8 t6 = gm_l2;
        gm_b0_1 t7 = gm_f_report_eardf07((void *)NULL, cx, NULL, t4, t5, t6);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_9huj44a F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* msg */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_b16_8 t5 = gm_l2;
        gm_b0_1 t6 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_jcnt4hm F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
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
    memmove(gm_out, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 56), 56);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0c2v515 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ppl3r45();
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
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5dhgg4i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_73v4ttb();
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
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6oos54v F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fa4sjvq();
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
inline __attribute__((aligned(4))) void gm_f_list_fvvkq82(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fvvkq82 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8ckfo47();
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_j5ps4qc(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_j5ps4qc F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8lf24cm();
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
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nof7vfj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rcd2jln();
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_g3tddrb_2();
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_t69m56meqefs8 t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    memset((uint8_t *)&t1 + 84, 0, 4);
    gm_t88rcucg1b7uk t2 = gm_l0;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t1 + 0, t2);
    memcpy((uint8_t *)&t1 + 8, ((uint8_t *)&F.l1), 64);
    gm_tl874t2atnlh3 t3 = gm_l2;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 72, t3);
    gm_tq7s98ihonkju t4 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3tttsr3hp1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_printer_h7jigd7 F;
    void * gm_l0; /* t */
    void * gm_l1; /* out */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    GM_ST(void *, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 0, t1);
    void * t2 = gm_l1;
    GM_ST(void *, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 8, t2);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 16, ((uint32_t)0ULL));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 20, ((uint32_t)0ULL));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bsig5q3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bsig5q3 F;
    void * gm_l0; /* l */
    gm_tpouv9r7lrri2 gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_8lf24cm();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_nl0tf27(t10, ((uint64_t)1ULL), t12);
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
        gm_tpouv9r7lrri2 t21 = GM_LD(gm_tpouv9r7lrri2, ((uint8_t *)a1));
        GM_ST(gm_tpouv9r7lrri2, ((uint8_t *)t20 + t18 * 32), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d0271rf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d0271rf F;
    void * gm_l0; /* l */
    gm_th2lqbo77pvsc gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_ppl3r45();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_e5frfkp(t10, ((uint64_t)1ULL), t12);
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
        gm_th2lqbo77pvsc t21 = gm_l1;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)t20 + t18 * 24), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 24)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d6c76ud_2 F;
    void * gm_l0; /* l */
    gm_trcmmd12cefb1 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_vj3ckdj_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_h08har5_2(t10, ((uint64_t)1ULL), t12);
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
        gm_trcmmd12cefb1 t21 = gm_l1;
        GM_ST(gm_trcmmd12cefb1, ((uint8_t *)t20 + t18 * 32), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d943vgp_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
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
            gm_b16_8 t11 = gm_f_empty_slice_g3tddrb_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_61fb705_2(t10, ((uint64_t)1ULL), t12);
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
        gm_t69m56meqefs8 t21 = F.l1;
        GM_ST(gm_t69m56meqefs8, ((uint8_t *)t20 + t18 * 88), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 88)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_jis5lhu F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
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
            gm_b16_8 t11 = gm_f_empty_slice_fa4sjvq();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_08uc6h5(t10, ((uint64_t)1ULL), t12);
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
        gm_t7te2tdpebbt4 t21 = F.l1;
        GM_ST(gm_t7te2tdpebbt4, ((uint8_t *)t20 + t18 * 144), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 144)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_k3l9nic F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
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
            gm_b16_8 t11 = gm_f_empty_slice_rcd2jln();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_j4iu2lk(t10, ((uint64_t)1ULL), t12);
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
        gm_t88rcucg1b7uk t21 = gm_l1;
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)t20 + t18 * 4), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 4)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l8mrumu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l8mrumu F;
    void * gm_l0; /* l */
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
            gm_b16_8 t11 = gm_f_empty_slice_8ckfo47();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_47d6kh3(t10, ((uint64_t)1ULL), t12);
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
        gm_tv1o1j44ii4db t21 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)a1));
        GM_ST(gm_tv1o1j44ii4db, ((uint8_t *)t20 + t18 * 136), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 136)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ucgmlo2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_73v4ttb();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_k4j2gj3(t10, ((uint64_t)1ULL), t12);
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
        uint64_t t21 = gm_l1;
        GM_ST(uint64_t, ((uint8_t *)t20 + t18 * 8), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 8)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_08uc6h5(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_08uc6h5 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)144ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_47d6kh3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_47d6kh3 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)136ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_61fb705_2 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)88ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_e5frfkp(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_e5frfkp F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h08har5_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j4iu2lk F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)4ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_k4j2gj3 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_nl0tf27(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_nl0tf27 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_report_eardf07 F;
    void * gm_l0; /* ds */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_b16_8 t4 = gm_l2;
        gm_t4gut2c9fj1kt t5;
        gm_f_error_s6hn871((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_push_o4ckdr1(t2, &t5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_8uijkbs F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t4;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_fsnl6fa F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t4;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 129u);
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
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_f_printer_h7jigd7((void *)&gm_l3, t2, t3);
    }
    {
        void * t4 = (void *)((uint8_t *)&gm_l3);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_b0_1 t6 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        uint8_t t9 = 0;
        F.ret = t8;
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
inline __attribute__((aligned(4))) void gm_f_synthetic_hdhcgpb(void *gm_out, const void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_synthetic_hdhcgpb F;
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 4, 0, 4);
    memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 84, 0, 4);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 0, ((uint32_t)4294967295ULL));
    memcpy((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 8, ((uint8_t *)a0), 64);
    gm_tl874t2atnlh3 t1 = gm_l1;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 72, t1);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)1ULL));
    gm_t88rcucg1b7uk t3 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
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
inline __attribute__((aligned(4))) void * gm_f_array_element_t6q5jtp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_array_element_t6q5jtp F;
    void * gm_l0; /* s */
    void * gm_l1; /* base */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    uint64_t gm_l3; /* i */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* index */
    GM_STACK_CHECK(&F, 353u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t5 = gm_l3;
        gm_tbci5ospk5nnn t6;
        gm_f_from_u64_ud13q5v((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t7;
        GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t7 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t7 + 8), t6);
        gm_tl874t2atnlh3 t8 = gm_l4;
        void * t9 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t4, &t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t9;
    }
    {
        gm_t88rcucg1b7uk t10 = gm_l2;
        gm_tlcl4u558l6p5 t11;
        memset((uint8_t *)&t11 + 17, 0, 7);
        void * t12 = gm_l1;
        GM_ST(void *, (uint8_t *)&t11 + 0, t12);
        void * t13 = gm_l5;
        GM_ST(void *, (uint8_t *)&t11 + 8, t13);
        GM_ST(uint8_t, (uint8_t *)&t11 + 16, ((uint8_t)0));
        gm_teti411eujvf4 t14;
        memset(&t14, 0, sizeof t14);
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)20ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t14 + 8), t11);
        gm_tl874t2atnlh3 t15 = gm_l4;
        void * t16 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t10, &t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t17 = 0;
        F.ret = t16;
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
__attribute__((aligned(4))) void * gm_f_elements_tuple_gdm5n6q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_elements_tuple_gdm5n6q F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* vt */
    gm_tlcm4lvu3g1a8 gm_l5; /* a */
    void * gm_l7; /* base */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* ty */
    void * gm_l14; /* agg */
    GM_STACK_CHECK(&F, 855u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l1;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t11;
    }
    {
        void * t12 = gm_l3;
        gm_t88rcucg1b7uk t13 = gm_l4;
        gm_t53at80pfdus3 t14;
        gm_f_kind_jcnt4hm((void *)&t14, t12, GM_CONV(gm_csguankko4lmhd, t13));
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        switch (t20) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma18; case ((uint8_t)2ULL): goto ma18; case ((uint8_t)3ULL): goto ma18; case ((uint8_t)4ULL): goto ma18; case ((uint8_t)5ULL): goto ma18; case ((uint8_t)6ULL): goto ma18; case ((uint8_t)7ULL): goto ma18; case ((uint8_t)8ULL): goto ma18; case ((uint8_t)9ULL): goto ma18; case ((uint8_t)10ULL): goto ma18; case ((uint8_t)11ULL): goto ma17; case ((uint8_t)12ULL): goto ma18; case ((uint8_t)13ULL): goto ma18; case ((uint8_t)14ULL): goto ma18; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma18; case ((uint8_t)17ULL): goto ma18; case ((uint8_t)18ULL): goto ma18; case ((uint8_t)19ULL): goto ma18; case ((uint8_t)20ULL): goto ma17; case ((uint8_t)21ULL): goto ma18; case ((uint8_t)22ULL): goto ma18; case ((uint8_t)23ULL): goto ma18; case ((uint8_t)24ULL): goto ma18; case ((uint8_t)25ULL): goto ma18; case ((uint8_t)26ULL): goto ma16; default: goto ma18; }
ma16:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t22 != ((uint8_t)26ULL)) goto mn21;
        uint8_t t23 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        goto me15;
mn21:;
ma17:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t27 != ((uint8_t)11ULL)) goto pa26;
        goto po25;
pa26:;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t29 != ((uint8_t)20ULL)) goto pa28;
        goto po25;
pa28:;
        goto mn24;
po25:;
        {
            void * t31 = gm_l0;
            gm_t88rcucg1b7uk t32 = gm_l4;
            gm_tl874t2atnlh3 t33 = gm_l2;
            gm_tb7u19loo3j87 t34;
            gm_f_spread_array_v7s332a((void *)&t34, (void *)NULL, cx, NULL, t31, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tlcm4lvu3g1a8 t35;
            if (((((uint8_t *)&t34))[16] != 0)) {
                gm_tlcm4lvu3g1a8 t36 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t34) + 0));
                t35 = t36;
            } else {
                uint8_t t37 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_l5 = t35;
        }
        {
            gm_f_list_ir0h3tc_2((void *)&F.l6, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t38 = gm_l0;
            void * t39 = gm_l1;
            void * t40 = (void *)((uint8_t *)&F.l6);
            void * t41 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t38, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t41;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l8, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_nof7vfj((void *)&F.l9, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
                gm_l10 = t43;
            }
            {
                for (;;) {
                    uint64_t t45 = gm_l11;
                    uint64_t t46 = gm_l10;
                    uint8_t t47 = (t45 < t46);
                    if (!t47) goto lb44;
                    {
                        uint64_t t50 = gm_l11;
                        uint64_t t53 = GM_LD(uint64_t, ((uint8_t *)&t50));
                        gm_l12 = t53;
                        {
                            void * t55 = (void *)((uint8_t *)&F.l8);
                            void * t56 = gm_l0;
                            void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
                            void * t58 = gm_l7;
                            gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
                            uint64_t t60 = gm_l12;
                            gm_tl874t2atnlh3 t61 = gm_l2;
                            void * t62 = gm_f_array_element_t6q5jtp((void *)NULL, cx, NULL, t57, t58, t59, t60, t61);
                            if (gm_unw.kind) {
                                goto gm_ll48;
                            }
                            gm_tftb1odc739t5 t63 = gm_f_push_d943vgp_2(t55, &(*(gm_t69m56meqefs8 *)((uint8_t *)t62)));
                            if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srcdjp1iiokf034, 182, 17);
                            gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
                        }
                        {
                            void * t65 = (void *)((uint8_t *)&F.l9);
                            gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
                            gm_tftb1odc739t5 t67 = gm_f_push_k3l9nic(t65, GM_CONV(gm_csguankko4lmhd, t66));
                            if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 1, gm_srcdjp1iiokf034, 183, 17);
                            gm_b0_1 t68 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 1));
                        }
                        goto me51;
mn52:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 181, 13);
me51:;
                    }
lc44:;
                    {
                        uint64_t t70 = gm_l11;
                        uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                        uint64_t t73 = r71;
                        gm_l11 = t73;
                    }
                }
gm_ll48:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb44; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc44; }
                }
                goto gm_lad;
lb44:;
            }
        }
        {
            void * t74 = gm_l3;
            gm_t6j6smj7qoctu t75;
            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
            GM_ST(gm_b16_8, (uint8_t *)&t75 + 0, t77);
            gm_b16_8 t78 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
            GM_ST(gm_b16_8, (uint8_t *)&t75 + 16, t79);
            gm_t53at80pfdus3 t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t80 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t80 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)12ULL));
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t80 + 8), t75);
            gm_t88rcucg1b7uk t81 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t74, &t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t81;
        }
        {
            gm_t88rcucg1b7uk t82 = gm_l13;
            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
            gm_teti411eujvf4 t84;
            memset(&t84, 0, sizeof t84);
            GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t84 + 8), t83);
            gm_tl874t2atnlh3 t85 = gm_l2;
            void * t86 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t82, &t84, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t86;
        }
        {
            uint64_t t88 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
            uint8_t t89 = ((t88 == ((uint64_t)0ULL)));
            if (!t89) goto ie87;
            {
                void * t91 = gm_l14;
                uint8_t t92 = 0;
                F.ret = t91;
                goto gm_fret;
            }
            goto ix87;
ie87:;
ix87:;
        }
        {
            gm_t88rcucg1b7uk t93 = gm_l13;
            gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            void * t95 = gm_l14;
            gm_tl874t2atnlh3 t96 = gm_l2;
            void * t97 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t93, t94, t95, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t98 = 0;
            F.ret = t97;
            goto gm_fret;
        }
        goto me15;
mn24:;
ma18:;
        void * t100 = gm_l1;
        uint8_t t101 = 0;
        F.ret = t100;
        goto gm_fret;
        goto me15;
mn99:;
ma19:;
        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 173, 5);
me15:;
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
__attribute__((aligned(4))) void gm_f_pack_rest_args_cknfrfl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_pack_rest_args_cknfrfl F;
    gm_b16_8 gm_l0; /* params */
    gm_b16_8 gm_l1; /* slot */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tf5srh4mp4b3i gm_l4; /* r */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint64_t gm_l9; /* ri */
    uint64_t gm_l10; /* before */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    uint64_t gm_l16; /* seen */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* k */
    gm_t08olq97rscn3 gm_l21; /* n */
    void * gm_l24; /* inner */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* k */
    gm_tl874t2atnlh3 gm_l30; /* sp */
    gm_tl874t2atnlh3 gm_l31; /* first */
    gm_tl874t2atnlh3 gm_l32; /* last */
    uint32_t gm_l33; /* at */
    uint64_t gm_l34; /* pos */
    uint64_t gm_l35; /* first */
    uint64_t gm_l36; /* n */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* k */
    GM_STACK_CHECK(&F, 3522u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_tf5srh4mp4b3i t2;
        memset(&t2, 0, sizeof t2);
        gm_l4 = t2;
    }
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l5 = t4;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l6;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    gm_tgsvmddj3e85e t11;
                    uint64_t t12 = gm_l6;
                    GM_ST(uint64_t, (uint8_t *)&t11 + 0, t12);
                    uint64_t t13 = gm_l6;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    memcpy((uint8_t *)&t11 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t14 * 56), 56);
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t11) + 0));
                    gm_l7 = t18;
                    F.l8 = (*(gm_t5v0c8ia6a5mo *)(((uint8_t *)&t11) + 8));
                    {
                        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 48));
                        if (!t21) goto ie20;
                        {
                            uint64_t t23 = gm_l7;
                            gm_tf5srh4mp4b3i t24;
                            GM_ST(uint8_t, (uint8_t *)&t24 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t24 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t24 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t24), t23);
                            ((uint8_t *)&t24)[8] = 1;
                            gm_l4 = t24;
                        }
                        {
                            goto lb5;
                        }
                        goto ix20;
ie20:;
ix20:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 14, 5);
me16:;
                }
lc5:;
                {
                    uint64_t t26 = gm_l6;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l6 = t29;
                }
            }
lb5:;
        }
    }
    {
        gm_tf5srh4mp4b3i t30 = gm_l4;
        uint64_t t31;
        if (((((uint8_t *)&t30))[8] != 0)) {
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&t30) + 0));
            t31 = t32;
        } else {
            gm_tck5nas0nf5v8 t33;
            memset(&t33, 0, sizeof t33);
            uint8_t t34 = 0;
            F.ret = t33;
            goto gm_fret;
        }
        gm_l9 = t31;
    }
    {
        gm_l10 = ((uint64_t)0ULL);
    }
    {
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            uint64_t t36 = gm_l9;
            gm_l11 = t36;
        }
        {
            for (;;) {
                uint64_t t38 = gm_l12;
                uint64_t t39 = gm_l11;
                uint8_t t40 = (t38 < t39);
                if (!t40) goto lb37;
                {
                    uint64_t t43 = gm_l12;
                    uint64_t t46 = GM_LD(uint64_t, ((uint8_t *)&t43));
                    gm_l13 = t46;
                    {
                        uint64_t t49 = gm_l13;
                        uint64_t t50 = (uintptr_t)t49;
                        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcdjp1iiokf034, 23, 12);
                        uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1));
                        if (!t52) goto ie48;
                        {
                            uint64_t t54 = gm_l10;
                            uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                            if (o56) gm_trap("integer overflow in `+`", gm_srcdjp1iiokf034, 23, 22);
                            uint64_t t57 = r55;
                            gm_l10 = t57;
                        }
                        goto ix48;
ie48:;
ix48:;
                    }
                    goto me44;
mn45:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 22, 5);
me44:;
                }
lc37:;
                {
                    uint64_t t59 = gm_l12;
                    uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                    uint64_t t62 = r60;
                    gm_l12 = t62;
                }
            }
lb37:;
        }
    }
    {
        gm_f_list_6oos54v((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l16 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t64 = gm_l2;
            gm_l17 = t64;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t66 = gm_l18;
                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                uint8_t t68 = (t66 < t67);
                if (!t68) goto lb65;
                {
                    gm_tl1vi0nd1uql9 t71;
                    uint64_t t72 = gm_l18;
                    GM_ST(uint64_t, (uint8_t *)&t71 + 0, t72);
                    uint64_t t73 = gm_l18;
                    uint64_t t74 = (uintptr_t)t73;
                    uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    memcpy((uint8_t *)&t71 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t74 * 144), 144);
                    uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&t71) + 0));
                    gm_l19 = t78;
                    F.l20 = (*(gm_t7te2tdpebbt4 *)(((uint8_t *)&t71) + 8));
                    {
                        uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 0) + 0));
                        switch (t85) { case ((uint8_t)0ULL): goto ma82; case ((uint8_t)1ULL): goto ma81; case ((uint8_t)2ULL): goto ma83; default: goto ma84; }
ma81:;
                        uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 0) + 0));
                        if (t87 != ((uint8_t)1ULL)) goto mn86;
                        gm_t08olq97rscn3 t88 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l20) + 0) + 8) + 0));
                        gm_l21 = t88;
                        {
                            gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                            uint64_t t92 = gm_l9;
                            uint64_t t93 = (uintptr_t)t92;
                            uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t93 >= (uintptr_t)(t94)) gm_trap_index(t93, t94, gm_srcdjp1iiokf034, 31, 30);
                            gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t93 * 56) + 0));
                            uint8_t t96 = (gm_eq0iqiubfm7e(((uint8_t *)&t91), ((uint8_t *)&t95)));
                            if (!t96) goto ie90;
                            {
                                gm_tbgd14esf0jvi t98;
                                memset((uint8_t *)&t98 + 12, 0, 4);
                                gm_tl874t2atnlh3 t99 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l20) + 128));
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t98 + 0, t99);
                                gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                                gm_tnpifv20irnph t101;
                                GM_ST(gm_b16_8, ((uint8_t *)&t101 + 0), t100);
                                gm_tabb710d9mq2e t102;
                                gm_f_format_l53b7vj((void *)&t102, (void *)NULL, cx, NULL, t101);
                                if (gm_unw.kind) {
                                    goto gm_ll69;
                                }
                                if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 8, gm_srcdjp1iiokf034, 32, 58);
                                gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&t102) + 8));
                                gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                                GM_ST(gm_b16_8, (uint8_t *)&t98 + 16, t104);
                                gm_ts5t31j0bugq1 t105;
                                GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t105 + 8), t98);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 41, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 42, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 44, 0);
                                ((uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out))[40] = 1;
                                GM_ST(gm_ts5t31j0bugq1, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 0, t105);
                                goto gm_fdone;
                            }
                            goto ix90;
ie90:;
ix90:;
                        }
                        {
                            void * t106 = (void *)((uint8_t *)&F.l14);
                            gm_tftb1odc739t5 t107 = gm_f_push_jis5lhu(t106, &(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l20)));
                            if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_srcdjp1iiokf034, 34, 17);
                            gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
                        }
                        goto me80;
mn86:;
ma82:;
                        uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 0) + 0));
                        if (t110 != ((uint8_t)0ULL)) goto mn109;
                        uint64_t t112 = gm_l16;
                        uint64_t t113 = gm_l10;
                        uint8_t t114 = (t112 < t113);
                        if (!t114) goto ie111;
                        {
                            uint64_t t116 = gm_l16;
                            uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                            if (o118) gm_trap("integer overflow in `+`", gm_srcdjp1iiokf034, 37, 17);
                            uint64_t t119 = r117;
                            gm_l16 = t119;
                        }
                        {
                            void * t120 = (void *)((uint8_t *)&F.l14);
                            gm_tftb1odc739t5 t121 = gm_f_push_jis5lhu(t120, &(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l20)));
                            if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 1, gm_srcdjp1iiokf034, 38, 17);
                            gm_b0_1 t122 = GM_LD(gm_b0_1, (((uint8_t *)&t121) + 1));
                        }
                        goto ix111;
ie111:;
                        {
                            void * t124 = (void *)((uint8_t *)&F.l15);
                            uint64_t t125 = gm_l19;
                            gm_tftb1odc739t5 t126 = gm_f_push_ucgmlo2(t124, t125);
                            if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_srcdjp1iiokf034, 40, 17);
                            gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
                        }
ix111:;
                        goto me80;
mn109:;
ma83:;
                        uint8_t t129 = GM_LD(uint8_t, ((((uint8_t *)&F.l20) + 0) + 0));
                        if (t129 != ((uint8_t)2ULL)) goto mn128;
                        {
                            uint64_t t132 = gm_l16;
                            uint64_t t133 = gm_l10;
                            uint8_t t134 = (t132 < t133);
                            if (!t134) goto ie131;
                            {
                                gm_tbgd14esf0jvi t136;
                                memset((uint8_t *)&t136 + 12, 0, 4);
                                gm_tl874t2atnlh3 t137 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l20) + 128));
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t136 + 0, t137);
                                gm_b16_8 t138;
                                GM_ST(void *, (uint8_t *)&t138, (void *)(gm_s2of8ntot1on0));
                                GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(80));
                                GM_ST(gm_b16_8, (uint8_t *)&t136 + 16, t138);
                                gm_ts5t31j0bugq1 t139;
                                GM_ST(uint8_t, (uint8_t *)&t139 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t139 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t139 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t139 + 8), t136);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 41, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 42, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 44, 0);
                                ((uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out))[40] = 1;
                                GM_ST(gm_ts5t31j0bugq1, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 0, t139);
                                goto gm_fdone;
                            }
                            goto ix131;
ie131:;
ix131:;
                        }
                        {
                            void * t140 = (void *)((uint8_t *)&F.l15);
                            uint64_t t141 = gm_l19;
                            gm_tftb1odc739t5 t142 = gm_f_push_ucgmlo2(t140, t141);
                            if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 1, gm_srcdjp1iiokf034, 44, 17);
                            gm_b0_1 t143 = GM_LD(gm_b0_1, (((uint8_t *)&t142) + 1));
                        }
                        goto me80;
mn128:;
ma84:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 29, 9);
me80:;
                    }
                    goto me76;
mn77:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 28, 5);
me76:;
                }
lc65:;
                {
                    uint64_t t145 = gm_l18;
                    uint64_t r146; bool o147 = __builtin_add_overflow(t145, ((uint64_t)1ULL), &r146);
                    uint64_t t148 = r146;
                    gm_l18 = t148;
                }
            }
gm_ll69:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb65; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc65; }
            }
            goto gm_lad;
lb65:;
        }
    }
    {
        memset((uint8_t *)&F.l22 + 140, 0, 4);
        gm_t2gtbh39ptekd t149;
        memset(&t149, 0, sizeof t149);
        GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)7ULL));
        gm_tl874t2atnlh3 t150 = gm_l3;
        gm_tc3oht4ksth9d t151;
        gm_f_synthetic_hdhcgpb((void *)&t151, &t149, GM_CONV(gm_cseahvg8v7p0vs, t150));
        gm_t22mtv3sd9prf t152;
        memset(&t152, 0, sizeof t152);
        GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t152 + 8), t151);
        GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&F.l22 + 0, t152);
        gm_tl874t2atnlh3 t153 = gm_l3;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l22 + 128, t153);
    }
    {
        uint64_t t155 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
        uint8_t t156 = ((t155 == ((uint64_t)1ULL)));
        if (!t156) goto ie154;
        uint64_t t157 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t158 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
        if (t157 >= (uintptr_t)(t158)) gm_trap_index(t157, t158, gm_srcdjp1iiokf034, 49, 51);
        uint64_t t159 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t157 * 8));
        uint64_t t160 = (uintptr_t)t159;
        uint64_t t161 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t160 >= (uintptr_t)(t161)) gm_trap_index(t160, t161, gm_srcdjp1iiokf034, 49, 46);
        uint8_t t162 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t160 * 144) + 0) + 0));
        if (t162 != ((uint8_t)2ULL)) goto ie154;
        gm_tc3oht4ksth9d t163 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t160 * 144) + 0) + 8));
        F.l23 = t163;
        {
            gm_tq7s98ihonkju t165 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l23)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k70n01rs29g)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t165))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t165) + 8, gm_srcdjp1iiokf034, 50, 18);
            void * t166 = GM_LD(void *, (((uint8_t *)&t165) + 8));
            gm_l24 = t166;
        }
        {
            memset((uint8_t *)&(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l22)) + 140, 0, 4);
            void * t167 = gm_l24;
            gm_t2gtbh39ptekd t168;
            memset(&t168, 0, sizeof t168);
            GM_ST(uint8_t, ((uint8_t *)&t168 + 0), ((uint8_t)11ULL));
            GM_ST(void *, ((uint8_t *)&t168 + 8), t167);
            gm_tl874t2atnlh3 t169 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l23) + 72));
            gm_tc3oht4ksth9d t170;
            gm_f_synthetic_hdhcgpb((void *)&t170, &t168, GM_CONV(gm_cseahvg8v7p0vs, t169));
            gm_t22mtv3sd9prf t171;
            memset(&t171, 0, sizeof t171);
            GM_ST(uint8_t, ((uint8_t *)&t171 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t171 + 8), t170);
            uint64_t t172 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t173 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
            if (t172 >= (uintptr_t)(t173)) gm_trap_index(t172, t173, gm_srcdjp1iiokf034, 51, 93);
            uint64_t t174 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t172 * 8));
            uint64_t t175 = (uintptr_t)t174;
            uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t175 >= (uintptr_t)(t176)) gm_trap_index(t175, t176, gm_srcdjp1iiokf034, 51, 88);
            gm_tl874t2atnlh3 t177 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t175 * 144) + 128));
            GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l22)) + 0, t171);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l22)) + 128, t177);
        }
        goto ix154;
ie154:;
        {
            gm_f_list_fvvkq82((void *)&F.l25, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
                gm_l26 = t180;
            }
            {
                gm_l27 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t182 = gm_l27;
                    uint64_t t183 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                    uint8_t t184 = (t182 < t183);
                    if (!t184) goto lb181;
                    {
                        uint64_t t187 = gm_l27;
                        uint64_t t188 = (uintptr_t)t187;
                        uint64_t t189 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                        uint64_t t190 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t188 * 8));
                        uint64_t t193 = GM_LD(uint64_t, ((uint8_t *)&t190));
                        gm_l28 = t193;
                        {
                            uint64_t t195 = gm_l28;
                            uint64_t t196 = (uintptr_t)t195;
                            uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t196 >= (uintptr_t)(t197)) gm_trap_index(t196, t197, gm_srcdjp1iiokf034, 55, 19);
                            uint8_t t200 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t196 * 144) + 0) + 0));
                            if (t200 != ((uint8_t)0ULL)) goto mn199;
                            gm_tc3oht4ksth9d t201 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t196 * 144) + 0) + 8));
                            F.l29 = t201;
                            void * t202 = (void *)((uint8_t *)&F.l25);
                            gm_tv1o1j44ii4db t203;
                            memset((uint8_t *)&t203 + 129, 0, 7);
                            gm_t1s75i08qtptd t204;
                            memset(&t204, 0, sizeof t204);
                            GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t203 + 0, t204);
                            memcpy((uint8_t *)&t203 + 40, ((uint8_t *)&F.l29), 88);
                            GM_ST(uint8_t, (uint8_t *)&t203 + 128, ((uint8_t)0));
                            gm_tftb1odc739t5 t205 = gm_f_push_l8mrumu(t202, &t203);
                            if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 1, gm_srcdjp1iiokf034, 56, 35);
                            gm_b0_1 t206 = GM_LD(gm_b0_1, (((uint8_t *)&t205) + 1));
                            goto me198;
mn199:;
                            gm_tbgd14esf0jvi t208;
                            memset((uint8_t *)&t208 + 12, 0, 4);
                            uint64_t t209 = gm_l28;
                            uint64_t t210 = (uintptr_t)t209;
                            uint64_t t211 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t210 >= (uintptr_t)(t211)) gm_trap_index(t210, t211, gm_srcdjp1iiokf034, 57, 42);
                            gm_tl874t2atnlh3 t212 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t210 * 144) + 128));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t208 + 0, t212);
                            gm_b16_8 t213;
                            GM_ST(void *, (uint8_t *)&t213, (void *)(gm_s1olfuo31ml9r));
                            GM_ST(uintptr_t, (uint8_t *)&t213 + 8, (uintptr_t)(63));
                            GM_ST(gm_b16_8, (uint8_t *)&t208 + 16, t213);
                            gm_ts5t31j0bugq1 t214;
                            GM_ST(uint8_t, (uint8_t *)&t214 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t214 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t214 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t214 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t214 + 8), t208);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 41, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 42, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 44, 0);
                            ((uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out))[40] = 1;
                            GM_ST(gm_ts5t31j0bugq1, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 0, t214);
                            goto gm_fdone;
                            goto me198;
mn207:;
                            gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 55, 13);
me198:;
                        }
                        goto me191;
mn192:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 54, 9);
me191:;
                    }
lc181:;
                    {
                        uint64_t t216 = gm_l27;
                        uint64_t r217; bool o218 = __builtin_add_overflow(t216, ((uint64_t)1ULL), &r217);
                        uint64_t t219 = r217;
                        gm_l27 = t219;
                    }
                }
lb181:;
            }
        }
        {
            gm_tl874t2atnlh3 t220 = gm_l3;
            gm_l30 = t220;
        }
        {
            uint64_t t222 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
            uint8_t t223 = (t222 > ((uint64_t)0ULL));
            if (!t223) goto ie221;
            {
                uint64_t t225 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t226 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                if (t225 >= (uintptr_t)(t226)) gm_trap_index(t225, t226, gm_srcdjp1iiokf034, 62, 27);
                uint64_t t227 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t225 * 8));
                uint64_t t228 = (uintptr_t)t227;
                uint64_t t229 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t228 >= (uintptr_t)(t229)) gm_trap_index(t228, t229, gm_srcdjp1iiokf034, 62, 22);
                gm_tl874t2atnlh3 t230 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t228 * 144) + 128));
                gm_l31 = t230;
            }
            {
                uint64_t t231 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                uint64_t r232; bool o233 = __builtin_sub_overflow(t231, ((uint64_t)1ULL), &r232);
                if (o233) gm_trap("integer overflow in `-`", gm_srcdjp1iiokf034, 63, 37);
                uint64_t t234 = r232;
                uint64_t t235 = (uintptr_t)t234;
                uint64_t t236 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                if (t235 >= (uintptr_t)(t236)) gm_trap_index(t235, t236, gm_srcdjp1iiokf034, 63, 26);
                uint64_t t237 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t235 * 8));
                uint64_t t238 = (uintptr_t)t237;
                uint64_t t239 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t238 >= (uintptr_t)(t239)) gm_trap_index(t238, t239, gm_srcdjp1iiokf034, 63, 21);
                gm_tl874t2atnlh3 t240 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t238 * 144) + 128));
                gm_l32 = t240;
            }
            {
                uint32_t t241 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 0));
                uint32_t t242 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 4));
                uint32_t t243 = GM_LD(uint32_t, (((uint8_t *)&gm_l32) + 8));
                gm_tl874t2atnlh3 t244;
                GM_ST(uint32_t, ((uint8_t *)&t244 + 0), t241);
                GM_ST(uint32_t, ((uint8_t *)&t244 + 4), t242);
                GM_ST(uint32_t, ((uint8_t *)&t244 + 8), t243);
                gm_l30 = t244;
            }
            goto ix221;
ie221:;
            {
                uint32_t t246 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
                uint32_t t247 = (t246 > ((uint32_t)1ULL) ? t246 : ((uint32_t)1ULL));
                uint32_t r248; bool o249 = __builtin_sub_overflow(t247, ((uint32_t)1ULL), &r248);
                if (o249) gm_trap("integer overflow in `-`", gm_srcdjp1iiokf034, 66, 19);
                uint32_t t250 = r248;
                gm_l33 = t250;
            }
            {
                uint32_t t251 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                uint32_t t252 = gm_l33;
                uint32_t t253 = gm_l33;
                gm_tl874t2atnlh3 t254;
                GM_ST(uint32_t, ((uint8_t *)&t254 + 0), t251);
                GM_ST(uint32_t, ((uint8_t *)&t254 + 4), t252);
                GM_ST(uint32_t, ((uint8_t *)&t254 + 8), t253);
                gm_l30 = t254;
            }
ix221:;
        }
        {
            memset((uint8_t *)&(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l22)) + 140, 0, 4);
            gm_b16_8 t255 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
            gm_t2gtbh39ptekd t256;
            memset(&t256, 0, sizeof t256);
            GM_ST(uint8_t, ((uint8_t *)&t256 + 0), ((uint8_t)10ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t256 + 8), t255);
            gm_tl874t2atnlh3 t257 = gm_l30;
            gm_tc3oht4ksth9d t258;
            gm_f_synthetic_hdhcgpb((void *)&t258, &t256, GM_CONV(gm_cseahvg8v7p0vs, t257));
            gm_t22mtv3sd9prf t259;
            memset(&t259, 0, sizeof t259);
            GM_ST(uint8_t, ((uint8_t *)&t259 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t259 + 8), t258);
            gm_tl874t2atnlh3 t260 = gm_l30;
            GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l22)) + 0, t259);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l22)) + 128, t260);
        }
ix154:;
    }
    {
        uint64_t t261 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
        gm_l34 = t261;
    }
    {
        uint64_t t263 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
        uint8_t t264 = (t263 > ((uint64_t)0ULL));
        if (!t264) goto ie262;
        {
            uint64_t t266 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t267 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
            if (t266 >= (uintptr_t)(t267)) gm_trap_index(t266, t267, gm_srcdjp1iiokf034, 75, 18);
            uint64_t t268 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t266 * 8));
            gm_l35 = t268;
        }
        {
            gm_l36 = ((uint64_t)0ULL);
        }
        {
            {
                gm_l38 = ((uint64_t)0ULL);
            }
            {
                uint64_t t270 = gm_l35;
                gm_l37 = t270;
            }
            {
                for (;;) {
                    uint64_t t272 = gm_l38;
                    uint64_t t273 = gm_l37;
                    uint8_t t274 = (t272 < t273);
                    if (!t274) goto lb271;
                    {
                        uint64_t t277 = gm_l38;
                        uint64_t t280 = GM_LD(uint64_t, ((uint8_t *)&t277));
                        gm_l39 = t280;
                        {
                            uint64_t t283 = gm_l39;
                            gm_b16_8 t284 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
                            uint8_t t285 = 0;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t284) + 8)); i++) if (gm_eq6groq37ram(((uint8_t *)GM_LD(void *, ((uint8_t *)&t284))) + i * 8, ((uint8_t *)&t283))) { t285 = 1; break; }
                            uint8_t t286 = (uint8_t)(!t285);
                            if (!t286) goto ie282;
                            {
                                uint64_t t288 = gm_l36;
                                uint64_t r289; bool o290 = __builtin_add_overflow(t288, ((uint64_t)1ULL), &r289);
                                if (o290) gm_trap("integer overflow in `+`", gm_srcdjp1iiokf034, 78, 38);
                                uint64_t t291 = r289;
                                gm_l36 = t291;
                            }
                            goto ix282;
ie282:;
ix282:;
                        }
                        goto me278;
mn279:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 77, 9);
me278:;
                    }
lc271:;
                    {
                        uint64_t t293 = gm_l38;
                        uint64_t r294; bool o295 = __builtin_add_overflow(t293, ((uint64_t)1ULL), &r294);
                        uint64_t t296 = r294;
                        gm_l38 = t296;
                    }
                }
lb271:;
            }
        }
        {
            uint64_t t297 = gm_l36;
            gm_l34 = t297;
        }
        goto ix262;
ie262:;
ix262:;
    }
    {
        void * t298 = (void *)((uint8_t *)&F.l14);
        uint64_t t299 = gm_l34;
        uint64_t t300 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
        uint64_t t301 = (t299 < t300 ? t299 : t300);
        gm_tftb1odc739t5 t302 = gm_f_insert_3qmicjr(t298, t301, &(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l22)));
        if (((((uint8_t *)&t302))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t302) + 1, gm_srcdjp1iiokf034, 82, 5);
        gm_b0_1 t303 = GM_LD(gm_b0_1, (((uint8_t *)&t302) + 1));
    }
    {
        gm_b16_8 t304 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        gm_ts5t31j0bugq1 t305;
        GM_ST(uint8_t, (uint8_t *)&t305 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t305 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t305 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t305 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t305 + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&t305 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t305 + 8), t304);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 44, 0);
        ((uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out))[40] = 1;
        GM_ST(gm_ts5t31j0bugq1, (uint8_t *)&(*(gm_tck5nas0nf5v8 *)gm_out) + 0, t305);
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
inline __attribute__((aligned(4))) void gm_f_spread_array_v7s332a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_spread_array_v7s332a F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* vt */
    gm_tl874t2atnlh3 gm_l2; /* span */
    uint64_t gm_l3; /* len */
    gm_t88rcucg1b7uk gm_l4; /* elem */
    GM_STACK_CHECK(&F, 454u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t14 != ((uint8_t)11ULL)) goto mn13;
        uint8_t t15 = GM_LD(uint8_t, (((((uint8_t *)&t6) + 8) + 12) + 0));
        if (t15 != ((uint8_t)1ULL)) goto mn13;
        {
            void * t17 = gm_l0;
            gm_tl874t2atnlh3 t18 = gm_l2;
            void * t19 = gm_l0;
            gm_t88rcucg1b7uk t20 = gm_l1;
            gm_b16_8 t21 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t22;
            GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t21);
            gm_tabb710d9mq2e t23;
            gm_f_format_q62qvnl((void *)&t23, (void *)NULL, cx, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_srcdjp1iiokf034, 150, 28);
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
            gm_b0_1 t26 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t17, t18, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tb7u19loo3j87 t27;
            memset(&t27, 0, sizeof t27);
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
        goto me7;
mn13:;
ma9:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t30 != ((uint8_t)11ULL)) goto mn29;
        uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l3 = t31;
        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 8));
        gm_l4 = t32;
        uint64_t t33 = gm_l3;
        gm_t88rcucg1b7uk t34 = gm_l4;
        gm_tlcm4lvu3g1a8 t35;
        GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t35 + 0), t33);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t35 + 8), t34);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tb7u19loo3j87 *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tb7u19loo3j87 *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tb7u19loo3j87 *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_tb7u19loo3j87 *)gm_out))[16] = 1;
        GM_ST(gm_tlcm4lvu3g1a8, (uint8_t *)&(*(gm_tb7u19loo3j87 *)gm_out) + 0, t35);
        goto gm_fdone;
        goto me7;
mn29:;
ma10:;
        {
            void * t38 = gm_l0;
            gm_tl874t2atnlh3 t39 = gm_l2;
            void * t40 = gm_l0;
            gm_t88rcucg1b7uk t41 = gm_l1;
            gm_b16_8 t42 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t43;
            GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t42);
            gm_tabb710d9mq2e t44;
            gm_f_format_p5cs5qn((void *)&t44, (void *)NULL, cx, NULL, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_srcdjp1iiokf034, 155, 28);
            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
            gm_b0_1 t47 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t38, t39, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tb7u19loo3j87 t48;
            memset(&t48, 0, sizeof t48);
            uint8_t t49 = 0;
            F.ret = t48;
            goto gm_fret;
        }
        goto me7;
mn36:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 148, 5);
me7:;
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
__attribute__((aligned(4))) void gm_f_spread_generic_args_8qtof2f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_spread_generic_args_8qtof2f F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    void * gm_l2; /* stmts */
    void * gm_l3; /* tys */
    uint64_t gm_l4; /* last_spread */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* k */
    gm_t08olq97rscn3 gm_l19; /* n */
    void * gm_l20; /* v */
    gm_t88rcucg1b7uk gm_l21; /* vt */
    gm_b16_8 gm_l22; /* name */
    uint32_t gm_l23; /* local */
    gm_t08olq97rscn3 gm_l26; /* n */
    gm_b16_8 gm_l28; /* es */
    gm_b16_8 gm_l29; /* ns */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* i */
    gm_tlcm4lvu3g1a8 gm_l33; /* arr */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* i */
    void * gm_l38; /* index */
    gm_th2lqbo77pvsc gm_l40; /* fs */
    gm_b16_8 gm_l41; /* f */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    gm_b16_8 gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    uint64_t gm_l47; /* i */
    gm_th2lqbo77pvsc gm_l48; /* p */
    gm_b16_8 gm_l49; /* n */
    gm_t08olq97rscn3 gm_l50; /* id */
    GM_STACK_CHECK(&F, 6367u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t6 = gm_l1;
            gm_l5 = t6;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l6;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    gm_tl1vi0nd1uql9 t13;
                    uint64_t t14 = gm_l6;
                    GM_ST(uint64_t, (uint8_t *)&t13 + 0, t14);
                    uint64_t t15 = gm_l6;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    memcpy((uint8_t *)&t13 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t16 * 144), 144);
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&t13) + 0));
                    gm_l7 = t20;
                    F.l8 = (*(gm_t7te2tdpebbt4 *)(((uint8_t *)&t13) + 8));
                    {
                        uint8_t t23;
                        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 0) + 0));
                        if (t26 != ((uint8_t)2ULL)) goto mn25;
                        t23 = ((uint8_t)1);
                        goto me24;
mn25:;
                        t23 = ((uint8_t)0);
                        goto me24;
mn27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 204, 12);
me24:;
                        if (!t23) goto ie22;
                        {
                            uint64_t t29 = gm_l7;
                            gm_l4 = t29;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 203, 5);
me18:;
                }
lc7:;
                {
                    uint64_t t31 = gm_l6;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l6 = t34;
                }
            }
lb7:;
        }
    }
    {
        gm_f_list_6oos54v((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t36 = gm_l1;
            gm_l10 = t36;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t38 = gm_l11;
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t40 = (t38 < t39);
                if (!t40) goto lb37;
                {
                    gm_tl1vi0nd1uql9 t43;
                    uint64_t t44 = gm_l11;
                    GM_ST(uint64_t, (uint8_t *)&t43 + 0, t44);
                    uint64_t t45 = gm_l11;
                    uint64_t t46 = (uintptr_t)t45;
                    uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    memcpy((uint8_t *)&t43 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t46 * 144), 144);
                    uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&t43) + 0));
                    gm_l12 = t50;
                    F.l13 = (*(gm_t7te2tdpebbt4 *)(((uint8_t *)&t43) + 8));
                    {
                        gm_tc3oht4ksth9d t52;
                        uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        if (t57 != ((uint8_t)2ULL)) goto pa56;
                        gm_tc3oht4ksth9d t58 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l13) + 0) + 8));
                        F.l14 = t58;
                        goto po55;
pa56:;
                        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        if (t60 != ((uint8_t)0ULL)) goto pa59;
                        gm_tc3oht4ksth9d t61 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l13) + 0) + 8));
                        F.l14 = t61;
                        goto po55;
pa59:;
                        goto mn54;
po55:;
                        gm_tc3oht4ksth9d t62 = F.l14;
                        t52 = t62;
                        goto me53;
mn54:;
                        uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        if (t64 != ((uint8_t)1ULL)) goto mn63;
                        gm_tc3oht4ksth9d t65 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)&F.l13) + 0) + 8) + 32));
                        F.l16 = t65;
                        gm_tc3oht4ksth9d t66 = F.l16;
                        t52 = t66;
                        goto me53;
mn63:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 208, 14);
me53:;
                        F.l17 = t52;
                    }
                    {
                        gm_t13p2dc04q01f t67;
                        memset(&t67, 0, sizeof t67);
                        F.l18 = t67;
                    }
                    {
                        uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        switch (t73) { case ((uint8_t)0ULL): goto ma70; case ((uint8_t)1ULL): goto ma71; case ((uint8_t)2ULL): goto ma69; default: goto ma72; }
ma69:;
                        uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        if (t75 != ((uint8_t)2ULL)) goto mn74;
                        goto me68;
mn74:;
ma70:;
                        uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        if (t78 != ((uint8_t)0ULL)) goto mn77;
                        {
                            uint64_t t81 = gm_l12;
                            uint64_t t82 = gm_l4;
                            uint8_t t83 = (t81 >= t82);
                            if (!t83) goto ie80;
                            {
                                void * t85 = (void *)((uint8_t *)&F.l9);
                                gm_tftb1odc739t5 t86 = gm_f_push_jis5lhu(t85, &(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l13)));
                                if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srcdjp1iiokf034, 218, 21);
                                gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
                            }
                            {
                                goto lc37;
                            }
                            goto ix80;
ie80:;
ix80:;
                        }
                        {
                            gm_t4vsf0g4hsv81 t88;
                            gm_t1s75i08qtptd t89;
                            memset(&t89, 0, sizeof t89);
                            GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t88 + 0, t89);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t13p2dc04q01f *)((uint8_t *)&F.l18)) + 41, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t13p2dc04q01f *)((uint8_t *)&F.l18)) + 42, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t13p2dc04q01f *)((uint8_t *)&F.l18)) + 44, 0);
                            ((uint8_t *)&(*(gm_t13p2dc04q01f *)((uint8_t *)&F.l18)))[40] = 1;
                            GM_ST(gm_t4vsf0g4hsv81, (uint8_t *)&(*(gm_t13p2dc04q01f *)((uint8_t *)&F.l18)) + 0, t88);
                        }
                        goto me68;
mn77:;
ma71:;
                        uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        if (t91 != ((uint8_t)1ULL)) goto mn90;
                        gm_t08olq97rscn3 t92 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l13) + 0) + 8) + 0));
                        gm_l19 = t92;
                        {
                            uint64_t t95 = gm_l12;
                            uint64_t t96 = gm_l4;
                            uint8_t t97 = (t95 >= t96);
                            if (!t97) goto ie94;
                            {
                                void * t99 = (void *)((uint8_t *)&F.l9);
                                gm_tftb1odc739t5 t100 = gm_f_push_jis5lhu(t99, &(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l13)));
                                if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_srcdjp1iiokf034, 225, 21);
                                gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
                            }
                            {
                                goto lc37;
                            }
                            goto ix94;
ie94:;
ix94:;
                        }
                        {
                            gm_t4vsf0g4hsv81 t102;
                            gm_t08olq97rscn3 t103 = gm_l19;
                            gm_t1s75i08qtptd t104;
                            GM_ST(uint8_t, (uint8_t *)&t104 + 33, 0);
                            GM_ST(uint16_t, (uint8_t *)&t104 + 34, 0);
                            GM_ST(uint32_t, (uint8_t *)&t104 + 36, 0);
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t104), t103);
                            ((uint8_t *)&t104)[32] = 1;
                            GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t102 + 0, t104);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t13p2dc04q01f *)((uint8_t *)&F.l18)) + 41, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t13p2dc04q01f *)((uint8_t *)&F.l18)) + 42, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t13p2dc04q01f *)((uint8_t *)&F.l18)) + 44, 0);
                            ((uint8_t *)&(*(gm_t13p2dc04q01f *)((uint8_t *)&F.l18)))[40] = 1;
                            GM_ST(gm_t4vsf0g4hsv81, (uint8_t *)&(*(gm_t13p2dc04q01f *)((uint8_t *)&F.l18)) + 0, t102);
                        }
                        goto me68;
mn90:;
ma72:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 214, 9);
me68:;
                    }
                    {
                        uint8_t t106;
                        if (((((uint8_t *)&F.l18))[40] != 0)) goto mn108;
                        t106 = ((uint8_t)0);
                        goto me107;
mn108:;
                        t106 = ((uint8_t)1);
                        goto me107;
mn109:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 232, 12);
me107:;
                        if (!t106) goto ie105;
                        uint8_t t110;
                        uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 8) + 0));
                        switch (t117) { case ((uint8_t)0ULL): goto ma113; case ((uint8_t)1ULL): goto ma115; case ((uint8_t)2ULL): goto ma114; case ((uint8_t)3ULL): goto ma115; case ((uint8_t)4ULL): goto ma115; case ((uint8_t)5ULL): goto ma115; case ((uint8_t)6ULL): goto ma112; case ((uint8_t)7ULL): goto ma115; case ((uint8_t)8ULL): goto ma115; case ((uint8_t)9ULL): goto ma115; case ((uint8_t)10ULL): goto ma115; case ((uint8_t)11ULL): goto ma115; case ((uint8_t)12ULL): goto ma115; case ((uint8_t)13ULL): goto ma115; case ((uint8_t)14ULL): goto ma115; case ((uint8_t)15ULL): goto ma115; case ((uint8_t)16ULL): goto ma115; case ((uint8_t)17ULL): goto ma115; case ((uint8_t)18ULL): goto ma115; case ((uint8_t)19ULL): goto ma115; case ((uint8_t)20ULL): goto ma115; case ((uint8_t)21ULL): goto ma115; case ((uint8_t)22ULL): goto ma115; case ((uint8_t)23ULL): goto ma115; case ((uint8_t)24ULL): goto ma115; case ((uint8_t)25ULL): goto ma115; case ((uint8_t)26ULL): goto ma115; case ((uint8_t)27ULL): goto ma115; case ((uint8_t)28ULL): goto ma115; case ((uint8_t)29ULL): goto ma115; case ((uint8_t)30ULL): goto ma115; case ((uint8_t)31ULL): goto ma115; case ((uint8_t)32ULL): goto ma115; case ((uint8_t)33ULL): goto ma115; case ((uint8_t)34ULL): goto ma115; case ((uint8_t)35ULL): goto ma115; case ((uint8_t)36ULL): goto ma115; case ((uint8_t)37ULL): goto ma115; case ((uint8_t)38ULL): goto ma115; case ((uint8_t)39ULL): goto ma115; case ((uint8_t)40ULL): goto ma115; case ((uint8_t)41ULL): goto ma115; case ((uint8_t)42ULL): goto ma115; case ((uint8_t)43ULL): goto ma115; case ((uint8_t)44ULL): goto ma115; case ((uint8_t)45ULL): goto ma115; case ((uint8_t)46ULL): goto ma115; case ((uint8_t)47ULL): goto ma115; case ((uint8_t)48ULL): goto ma115; case ((uint8_t)49ULL): goto ma115; case ((uint8_t)50ULL): goto ma115; default: goto ma115; }
ma112:;
                        uint8_t t119 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 8) + 0));
                        if (t119 != ((uint8_t)6ULL)) goto mn118;
                        t110 = ((uint8_t)1);
                        goto me111;
mn118:;
ma113:;
                        uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 8) + 0));
                        if (t121 != ((uint8_t)0ULL)) goto mn120;
                        t110 = ((uint8_t)1);
                        goto me111;
mn120:;
ma114:;
                        uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 8) + 0));
                        if (t123 != ((uint8_t)2ULL)) goto mn122;
                        t110 = ((uint8_t)1);
                        goto me111;
mn122:;
ma115:;
                        t110 = ((uint8_t)0);
                        goto me111;
mn124:;
ma116:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 232, 34);
me111:;
                        if (!t110) goto ie105;
                        {
                            void * t126 = (void *)((uint8_t *)&F.l9);
                            gm_tftb1odc739t5 t127 = gm_f_push_jis5lhu(t126, &(*(gm_t7te2tdpebbt4 *)((uint8_t *)&F.l13)));
                            if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 1, gm_srcdjp1iiokf034, 233, 13);
                            gm_b0_1 t128 = GM_LD(gm_b0_1, (((uint8_t *)&t127) + 1));
                        }
                        {
                            goto lc37;
                        }
                        goto ix105;
ie105:;
ix105:;
                    }
                    {
                        void * t129 = gm_l0;
                        void * t130 = (void *)((uint8_t *)&F.l17);
                        gm_tkn42u7na3t81 t131;
                        memset(&t131, 0, sizeof t131);
                        void * t132 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t129, t130, t131);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        gm_l20 = t132;
                    }
                    {
                        void * t133 = gm_l0;
                        void * t134 = (void *)(((uint8_t *)t133) + 96);
                        void * t135 = gm_l0;
                        void * t136 = GM_LD(void *, (((uint8_t *)t135) + 0));
                        void * t137 = gm_l20;
                        gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t137) + 0));
                        gm_t88rcucg1b7uk t139 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t134, t136, t138);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        gm_l21 = t139;
                    }
                    {
                        void * t140 = gm_l0;
                        uint64_t t141 = GM_LD(uintptr_t, ((((uint8_t *)t140) + 16) + 0) + 8);
                        gm_t4ce0jd3tdro4 t142;
                        GM_ST(uint64_t, ((uint8_t *)&t142 + 0), t141);
                        gm_tabb710d9mq2e t143;
                        gm_f_format_uubl3s3((void *)&t143, (void *)NULL, cx, NULL, t142);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 8, gm_srcdjp1iiokf034, 238, 17);
                        gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&t143) + 8));
                        gm_l22 = t144;
                    }
                    {
                        void * t145 = gm_l0;
                        gm_b16_8 t146 = gm_l22;
                        gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
                        gm_t88rcucg1b7uk t148 = gm_l21;
                        gm_tl874t2atnlh3 t149 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                        uint32_t t150 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t145, t147, t148, ((uint8_t)0), t149);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        gm_l23 = t150;
                    }
                    {
                        void * t151 = gm_l2;
                        uint32_t t152 = gm_l23;
                        void * t153 = gm_l20;
                        gm_ttggq7ng4kp3p t154;
                        GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t154 + 0), t152);
                        GM_ST(void *, ((uint8_t *)&t154 + 8), t153);
                        gm_trcmmd12cefb1 t155;
                        GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t155 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t155 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t155 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t155 + 8), t154);
                        gm_tftb1odc739t5 t156 = gm_f_push_d6c76ud_2(t151, &t155);
                        if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 1, gm_srcdjp1iiokf034, 240, 9);
                        gm_b0_1 t157 = GM_LD(gm_b0_1, (((uint8_t *)&t156) + 1));
                    }
                    {
                        void * t158 = gm_l0;
                        gm_b16_8 t159 = gm_l22;
                        gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
                        uint32_t t161 = gm_l23;
                        gm_t930q5l38hdap t162;
                        memset(&t162, 0, sizeof t162);
                        GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t162 + 8), t161);
                        gm_tl874t2atnlh3 t163 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                        gm_b0_1 t164 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t158, t160, &t162, t163);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                    }
                    {
                        gm_t08olq97rscn3 t165;
                        memset((uint8_t *)&t165 + 28, 0, 4);
                        gm_b16_8 t166 = gm_l22;
                        gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
                        GM_ST(gm_b16_8, (uint8_t *)&t165 + 0, t167);
                        gm_tl874t2atnlh3 t168 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t165 + 16, t168);
                        gm_t2gtbh39ptekd t169;
                        GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t169 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t169 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t169 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&t169 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)6ULL));
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t169 + 8), t165);
                        gm_tl874t2atnlh3 t170 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                        gm_f_synthetic_hdhcgpb((void *)&F.l24, &t169, GM_CONV(gm_cseahvg8v7p0vs, t170));
                    }
                    {
                        if (!((((uint8_t *)&F.l18))[40] != 0)) goto ie171;
                        gm_t4vsf0g4hsv81 t172 = GM_LD(gm_t4vsf0g4hsv81, ((uint8_t *)&F.l18));
                        F.l25 = t172;
                        {
                            if (!(((((uint8_t *)&F.l25) + 0))[32] != 0)) goto ie174;
                            gm_t08olq97rscn3 t175 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l25) + 0));
                            gm_l26 = t175;
                            {
                                void * t177 = (void *)((uint8_t *)&F.l9);
                                gm_t7te2tdpebbt4 t178;
                                memset((uint8_t *)&t178 + 140, 0, 4);
                                gm_t56mj5hnm2uc6 t179;
                                memcpy((uint8_t *)&t179 + 0, ((uint8_t *)&gm_l26), 32);
                                memcpy((uint8_t *)&t179 + 32, ((uint8_t *)&F.l24), 88);
                                gm_t22mtv3sd9prf t180;
                                GM_ST(uint8_t, (uint8_t *)&t180 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t180 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t180 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t180 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_t56mj5hnm2uc6, ((uint8_t *)&t180 + 8), t179);
                                GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t178 + 0, t180);
                                gm_tl874t2atnlh3 t181 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l13) + 128));
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t178 + 128, t181);
                                gm_tftb1odc739t5 t182 = gm_f_push_jis5lhu(t177, &t178);
                                if (((((uint8_t *)&t182))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t182) + 1, gm_srcdjp1iiokf034, 245, 17);
                                gm_b0_1 t183 = GM_LD(gm_b0_1, (((uint8_t *)&t182) + 1));
                            }
                            goto ix174;
ie174:;
                            {
                                void * t185 = (void *)((uint8_t *)&F.l9);
                                gm_t7te2tdpebbt4 t186;
                                memset((uint8_t *)&t186 + 140, 0, 4);
                                gm_tc3oht4ksth9d t187 = F.l24;
                                gm_t22mtv3sd9prf t188;
                                memset(&t188, 0, sizeof t188);
                                GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t188 + 8), t187);
                                GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t186 + 0, t188);
                                gm_tl874t2atnlh3 t189 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l13) + 128));
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t186 + 128, t189);
                                gm_tftb1odc739t5 t190 = gm_f_push_jis5lhu(t185, &t186);
                                if (((((uint8_t *)&t190))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t190) + 1, gm_srcdjp1iiokf034, 247, 17);
                                gm_b0_1 t191 = GM_LD(gm_b0_1, (((uint8_t *)&t190) + 1));
                            }
ix174:;
                        }
                        {
                            goto lc37;
                        }
                        goto ix171;
ie171:;
ix171:;
                    }
                    {
                        gm_f_list_0c2v515((void *)&F.l27, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                    }
                    {
                        void * t192 = gm_l3;
                        gm_t88rcucg1b7uk t193 = gm_l21;
                        gm_t53at80pfdus3 t194;
                        gm_f_kind_jcnt4hm((void *)&t194, t192, GM_CONV(gm_csguankko4lmhd, t193));
                        uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&t194) + 0));
                        switch (t201) { case ((uint8_t)0ULL): goto ma199; case ((uint8_t)1ULL): goto ma199; case ((uint8_t)2ULL): goto ma199; case ((uint8_t)3ULL): goto ma199; case ((uint8_t)4ULL): goto ma199; case ((uint8_t)5ULL): goto ma199; case ((uint8_t)6ULL): goto ma199; case ((uint8_t)7ULL): goto ma199; case ((uint8_t)8ULL): goto ma199; case ((uint8_t)9ULL): goto ma199; case ((uint8_t)10ULL): goto ma199; case ((uint8_t)11ULL): goto ma198; case ((uint8_t)12ULL): goto ma196; case ((uint8_t)13ULL): goto ma199; case ((uint8_t)14ULL): goto ma199; case ((uint8_t)15ULL): goto ma199; case ((uint8_t)16ULL): goto ma199; case ((uint8_t)17ULL): goto ma199; case ((uint8_t)18ULL): goto ma199; case ((uint8_t)19ULL): goto ma199; case ((uint8_t)20ULL): goto ma198; case ((uint8_t)21ULL): goto ma199; case ((uint8_t)22ULL): goto ma199; case ((uint8_t)23ULL): goto ma199; case ((uint8_t)24ULL): goto ma199; case ((uint8_t)25ULL): goto ma199; case ((uint8_t)26ULL): goto ma197; default: goto ma199; }
ma196:;
                        uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&t194) + 0));
                        if (t203 != ((uint8_t)12ULL)) goto mn202;
                        gm_b16_8 t204 = GM_LD(gm_b16_8, ((((uint8_t *)&t194) + 8) + 0));
                        gm_l28 = t204;
                        gm_b16_8 t205 = GM_LD(gm_b16_8, ((((uint8_t *)&t194) + 8) + 16));
                        gm_l29 = t205;
                        {
                            gm_l31 = ((uint64_t)0ULL);
                        }
                        {
                            uint64_t t207 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                            gm_l30 = t207;
                        }
                        {
                            for (;;) {
                                uint64_t t209 = gm_l31;
                                uint64_t t210 = gm_l30;
                                uint8_t t211 = (t209 < t210);
                                if (!t211) goto lb208;
                                {
                                    uint64_t t214 = gm_l31;
                                    uint64_t t217 = GM_LD(uint64_t, ((uint8_t *)&t214));
                                    gm_l32 = t217;
                                    {
                                        void * t219 = (void *)((uint8_t *)&F.l27);
                                        gm_th2lqbo77pvsc t220;
                                        uint64_t t222 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                                        uint8_t t223 = (t222 > ((uint64_t)0ULL));
                                        if (!t223) goto ie221;
                                        uint64_t t225 = gm_l32;
                                        uint64_t t226 = (uintptr_t)t225;
                                        uint64_t t227 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                                        if (t226 >= (uintptr_t)(t227)) gm_trap_index(t226, t227, gm_srcdjp1iiokf034, 253, 79);
                                        gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t226 * 16));
                                        gm_b16_8 t229 = t228;
                                        gm_th2lqbo77pvsc t230;
                                        GM_ST(uint8_t, (uint8_t *)&t230 + 17, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t230 + 18, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t230 + 20, 0);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t230), t229);
                                        ((uint8_t *)&t230)[16] = 1;
                                        t220 = t230;
                                        goto ix221;
ie221:;
                                        gm_th2lqbo77pvsc t232;
                                        memset(&t232, 0, sizeof t232);
                                        gm_th2lqbo77pvsc t233 = t232;
                                        t220 = t233;
ix221:;
                                        gm_tftb1odc739t5 t234 = gm_f_push_d0271rf(t219, &t220);
                                        if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 1, gm_srcdjp1iiokf034, 253, 52);
                                        gm_b0_1 t235 = GM_LD(gm_b0_1, (((uint8_t *)&t234) + 1));
                                    }
                                    goto me215;
mn216:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 253, 31);
me215:;
                                }
lc208:;
                                {
                                    uint64_t t237 = gm_l31;
                                    uint64_t r238; bool o239 = __builtin_add_overflow(t237, ((uint64_t)1ULL), &r238);
                                    uint64_t t240 = r238;
                                    gm_l31 = t240;
                                }
                            }
lb208:;
                        }
                        goto me195;
mn202:;
ma197:;
                        uint8_t t242 = GM_LD(uint8_t, (((uint8_t *)&t194) + 0));
                        if (t242 != ((uint8_t)26ULL)) goto mn241;
                        gm_th2lqbo77pvsc t243;
                        memset(&t243, 0, sizeof t243);
                        uint8_t t244 = 0;
                        F.ret = t243;
                        goto gm_fret;
                        goto me195;
mn241:;
ma198:;
                        uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)&t194) + 0));
                        if (t248 != ((uint8_t)11ULL)) goto pa247;
                        goto po246;
pa247:;
                        uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)&t194) + 0));
                        if (t250 != ((uint8_t)20ULL)) goto pa249;
                        goto po246;
pa249:;
                        goto mn245;
po246:;
                        {
                            void * t252 = gm_l0;
                            gm_t88rcucg1b7uk t253 = gm_l21;
                            gm_tl874t2atnlh3 t254 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                            gm_tb7u19loo3j87 t255;
                            gm_f_spread_array_v7s332a((void *)&t255, (void *)NULL, cx, NULL, t252, t253, t254);
                            if (gm_unw.kind) {
                                goto gm_ll41;
                            }
                            gm_tlcm4lvu3g1a8 t256;
                            if (((((uint8_t *)&t255))[16] != 0)) {
                                gm_tlcm4lvu3g1a8 t257 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t255) + 0));
                                t256 = t257;
                            } else {
                                gm_th2lqbo77pvsc t258;
                                memset(&t258, 0, sizeof t258);
                                uint8_t t259 = 0;
                                F.ret = t258;
                                goto gm_fret;
                            }
                            gm_l33 = t256;
                        }
                        {
                            {
                                gm_l35 = ((uint64_t)0ULL);
                            }
                            {
                                uint64_t t261 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 0));
                                gm_l34 = t261;
                            }
                            {
                                for (;;) {
                                    uint64_t t263 = gm_l35;
                                    uint64_t t264 = gm_l34;
                                    uint8_t t265 = (t263 < t264);
                                    if (!t265) goto lb262;
                                    {
                                        uint64_t t268 = gm_l35;
                                        uint64_t t271 = GM_LD(uint64_t, ((uint8_t *)&t268));
                                        gm_l36 = t271;
                                        {
                                            memset((uint8_t *)&F.l37 + 61, 0, 3);
                                            uint64_t t273 = gm_l36;
                                            gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l37 + 0)), (void *)NULL, cx, NULL, t273);
                                            if (gm_unw.kind) {
                                                goto gm_ll266;
                                            }
                                            gm_t46lmulm8gaj3 t274;
                                            memset(&t274, 0, sizeof t274);
                                            GM_ST(gm_t46lmulm8gaj3, (uint8_t *)&F.l37 + 48, t274);
                                            GM_ST(uint8_t, (uint8_t *)&F.l37 + 60, ((uint8_t)0));
                                        }
                                        {
                                            gm_tq7s98ihonkju t275 = gm_f_new_q1fs5ps((void *)NULL, cx, NULL, &(*(gm_ta42iqot1auae *)((uint8_t *)&F.l37)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjn96jdce98)));
                                            if (gm_unw.kind) {
                                                goto gm_ll266;
                                            }
                                            if (((((uint8_t *)&t275))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t275) + 8, gm_srcdjp1iiokf034, 259, 49);
                                            void * t276 = GM_LD(void *, (((uint8_t *)&t275) + 8));
                                            gm_t2gtbh39ptekd t277;
                                            memset(&t277, 0, sizeof t277);
                                            GM_ST(uint8_t, ((uint8_t *)&t277 + 0), ((uint8_t)0ULL));
                                            GM_ST(void *, ((uint8_t *)&t277 + 8), t276);
                                            gm_tl874t2atnlh3 t278 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            gm_tc3oht4ksth9d t279;
                                            gm_f_synthetic_hdhcgpb((void *)&t279, &t277, GM_CONV(gm_cseahvg8v7p0vs, t278));
                                            gm_tq7s98ihonkju t280 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &t279, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kqn03v7t2qt)));
                                            if (gm_unw.kind) {
                                                goto gm_ll266;
                                            }
                                            if (((((uint8_t *)&t280))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t280) + 8, gm_srcdjp1iiokf034, 259, 30);
                                            void * t281 = GM_LD(void *, (((uint8_t *)&t280) + 8));
                                            gm_l38 = t281;
                                        }
                                        {
                                            gm_tq7s98ihonkju t282 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l24)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kbjtngq55qv)));
                                            if (gm_unw.kind) {
                                                goto gm_ll266;
                                            }
                                            if (((((uint8_t *)&t282))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t282) + 8, gm_srcdjp1iiokf034, 260, 46);
                                            void * t283 = GM_LD(void *, (((uint8_t *)&t282) + 8));
                                            void * t284 = gm_l38;
                                            gm_t3mf58gv9kf3n t285;
                                            GM_ST(void *, ((uint8_t *)&t285 + 0), t283);
                                            GM_ST(void *, ((uint8_t *)&t285 + 8), t284);
                                            gm_t2gtbh39ptekd t286;
                                            memset(&t286, 0, sizeof t286);
                                            GM_ST(uint8_t, ((uint8_t *)&t286 + 0), ((uint8_t)20ULL));
                                            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t286 + 8), t285);
                                            gm_tl874t2atnlh3 t287 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            gm_f_synthetic_hdhcgpb((void *)&F.l39, &t286, GM_CONV(gm_cseahvg8v7p0vs, t287));
                                        }
                                        {
                                            void * t288 = (void *)((uint8_t *)&F.l9);
                                            gm_t7te2tdpebbt4 t289;
                                            memset((uint8_t *)&t289 + 140, 0, 4);
                                            gm_tc3oht4ksth9d t290 = F.l39;
                                            gm_t22mtv3sd9prf t291;
                                            memset(&t291, 0, sizeof t291);
                                            GM_ST(uint8_t, ((uint8_t *)&t291 + 0), ((uint8_t)0ULL));
                                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t291 + 8), t290);
                                            GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t289 + 0, t291);
                                            gm_tl874t2atnlh3 t292 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t289 + 128, t292);
                                            gm_tftb1odc739t5 t293 = gm_f_push_jis5lhu(t288, &t289);
                                            if (((((uint8_t *)&t293))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t293) + 1, gm_srcdjp1iiokf034, 261, 21);
                                            gm_b0_1 t294 = GM_LD(gm_b0_1, (((uint8_t *)&t293) + 1));
                                        }
                                        goto me269;
mn270:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 257, 17);
me269:;
                                    }
lc262:;
                                    {
                                        uint64_t t296 = gm_l35;
                                        uint64_t r297; bool o298 = __builtin_add_overflow(t296, ((uint64_t)1ULL), &r297);
                                        uint64_t t299 = r297;
                                        gm_l35 = t299;
                                    }
                                }
gm_ll266:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb262; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc262; }
                                }
                                goto gm_ll41;
lb262:;
                            }
                        }
                        {
                            goto lc37;
                        }
                        goto me195;
mn245:;
ma199:;
                        {
                            void * t302 = gm_l0;
                            void * t303 = GM_LD(void *, (((uint8_t *)t302) + 0));
                            gm_t88rcucg1b7uk t304 = gm_l21;
                            gm_f_struct_fields_chqu3it((void *)&gm_l40, t303, GM_CONV(gm_csguankko4lmhd, t304));
                        }
                        {
                            if (!((((uint8_t *)&gm_l40))[16] != 0)) goto ie305;
                            gm_b16_8 t306 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l40));
                            gm_l41 = t306;
                            void * t307 = gm_l0;
                            gm_t88rcucg1b7uk t308 = gm_l21;
                            gm_ttu09lij36f8c t309;
                            gm_f_hidden_field_u7uao2s((void *)&t309, t307, GM_CONV(gm_csguankko4lmhd, t308));
                            uint8_t t310;
                            if (((((uint8_t *)&t309))[32] != 0)) goto mn312;
                            t310 = ((uint8_t)1);
                            goto me311;
mn312:;
                            t310 = ((uint8_t)0);
                            goto me311;
mn313:;
                            gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 267, 32);
me311:;
                            if (!t310) goto ie305;
                            {
                                {
                                    gm_b16_8 t316 = gm_l41;
                                    gm_l42 = t316;
                                }
                                {
                                    gm_l43 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t318 = gm_l43;
                                        uint64_t t319 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                        uint8_t t320 = (t318 < t319);
                                        if (!t320) goto lb317;
                                        {
                                            uint64_t t323 = gm_l43;
                                            uint64_t t324 = (uintptr_t)t323;
                                            uint64_t t325 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                            gm_t0mglksrkfa5e t328 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t324 * 40));
                                            F.l44 = t328;
                                            {
                                                void * t330 = (void *)((uint8_t *)&F.l27);
                                                gm_b16_8 t331 = GM_LD(gm_b16_8, (((uint8_t *)&F.l44) + 0));
                                                gm_th2lqbo77pvsc t332;
                                                GM_ST(uint8_t, (uint8_t *)&t332 + 17, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t332 + 18, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t332 + 20, 0);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t332), t331);
                                                ((uint8_t *)&t332)[16] = 1;
                                                gm_tftb1odc739t5 t333 = gm_f_push_d0271rf(t330, &t332);
                                                if (((((uint8_t *)&t333))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t333) + 1, gm_srcdjp1iiokf034, 268, 34);
                                                gm_b0_1 t334 = GM_LD(gm_b0_1, (((uint8_t *)&t333) + 1));
                                            }
                                            goto me326;
mn327:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 268, 21);
me326:;
                                        }
lc317:;
                                        {
                                            uint64_t t336 = gm_l43;
                                            uint64_t r337; bool o338 = __builtin_add_overflow(t336, ((uint64_t)1ULL), &r337);
                                            uint64_t t339 = r337;
                                            gm_l43 = t339;
                                        }
                                    }
lb317:;
                                }
                            }
                            goto ix305;
ie305:;
                            {
                                void * t341 = gm_l0;
                                gm_tl874t2atnlh3 t342 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                void * t343 = gm_l0;
                                gm_t88rcucg1b7uk t344 = gm_l21;
                                gm_b16_8 t345 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t343, t344);
                                if (gm_unw.kind) {
                                    goto gm_ll41;
                                }
                                gm_tnpifv20irnph t346;
                                GM_ST(gm_b16_8, ((uint8_t *)&t346 + 0), t345);
                                gm_tabb710d9mq2e t347;
                                gm_f_format_7f8ekea((void *)&t347, (void *)NULL, cx, NULL, t346);
                                if (gm_unw.kind) {
                                    goto gm_ll41;
                                }
                                if (((((uint8_t *)&t347))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t347) + 8, gm_srcdjp1iiokf034, 270, 38);
                                gm_b16_8 t348 = GM_LD(gm_b16_8, (((uint8_t *)&t347) + 8));
                                gm_b16_8 t349 = GM_LD(gm_b16_8, ((uint8_t *)&t348));
                                gm_b0_1 t350 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t341, t342, t349);
                                if (gm_unw.kind) {
                                    goto gm_ll41;
                                }
                            }
                            {
                                gm_th2lqbo77pvsc t351;
                                memset(&t351, 0, sizeof t351);
                                uint8_t t352 = 0;
                                F.ret = t351;
                                goto gm_fret;
                            }
ix305:;
                        }
                        goto me195;
mn300:;
ma200:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 252, 9);
me195:;
                    }
                    {
                        {
                            gm_b16_8 t354 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
                            gm_b16_8 t355 = GM_LD(gm_b16_8, ((uint8_t *)&t354));
                            gm_l45 = t355;
                        }
                        {
                            gm_l46 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t357 = gm_l46;
                                uint64_t t358 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                                uint8_t t359 = (t357 < t358);
                                if (!t359) goto lb356;
                                {
                                    gm_t13urt52695sh t362;
                                    uint64_t t363 = gm_l46;
                                    GM_ST(uint64_t, (uint8_t *)&t362 + 0, t363);
                                    uint64_t t364 = gm_l46;
                                    uint64_t t365 = (uintptr_t)t364;
                                    uint64_t t366 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                                    memcpy((uint8_t *)&t362 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l45)) + t365 * 24), 24);
                                    uint64_t t369 = GM_LD(uint64_t, (((uint8_t *)&t362) + 0));
                                    gm_l47 = t369;
                                    gm_l48 = (*(gm_th2lqbo77pvsc *)(((uint8_t *)&t362) + 8));
                                    {
                                        if (!((((uint8_t *)&gm_l48))[16] != 0)) goto ie371;
                                        gm_b16_8 t372 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l48));
                                        gm_l49 = t372;
                                        {
                                            memset((uint8_t *)&gm_l50 + 28, 0, 4);
                                            gm_b16_8 t374 = gm_l49;
                                            GM_ST(gm_b16_8, (uint8_t *)&gm_l50 + 0, t374);
                                            gm_tl874t2atnlh3 t375 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&gm_l50 + 16, t375);
                                        }
                                        {
                                            void * t376 = (void *)((uint8_t *)&F.l9);
                                            gm_t7te2tdpebbt4 t377;
                                            memset((uint8_t *)&t377 + 140, 0, 4);
                                            gm_t56mj5hnm2uc6 t378;
                                            memcpy((uint8_t *)&t378 + 0, ((uint8_t *)&gm_l50), 32);
                                            gm_tioie2ju3q4ul t379;
                                            gm_tq7s98ihonkju t380 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l24)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ka2mtmecqk3)));
                                            if (gm_unw.kind) {
                                                goto gm_ll360;
                                            }
                                            if (((((uint8_t *)&t380))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t380) + 8, gm_srcdjp1iiokf034, 278, 72);
                                            void * t381 = GM_LD(void *, (((uint8_t *)&t380) + 8));
                                            GM_ST(void *, (uint8_t *)&t379 + 0, t381);
                                            memcpy((uint8_t *)&t379 + 8, ((uint8_t *)&gm_l50), 32);
                                            gm_t2gtbh39ptekd t382;
                                            GM_ST(uint8_t, (uint8_t *)&t382 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t382 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t382 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t382 + 48, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t382 + 56, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t382 + 0), ((uint8_t)22ULL));
                                            GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t382 + 8), t379);
                                            gm_tl874t2atnlh3 t383 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            gm_f_synthetic_hdhcgpb((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&t378 + 32)), &t382, GM_CONV(gm_cseahvg8v7p0vs, t383));
                                            gm_t22mtv3sd9prf t384;
                                            GM_ST(uint8_t, (uint8_t *)&t384 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t384 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t384 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t384 + 0), ((uint8_t)1ULL));
                                            GM_ST(gm_t56mj5hnm2uc6, ((uint8_t *)&t384 + 8), t378);
                                            GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t377 + 0, t384);
                                            gm_tl874t2atnlh3 t385 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t377 + 128, t385);
                                            gm_tftb1odc739t5 t386 = gm_f_push_jis5lhu(t376, &t377);
                                            if (((((uint8_t *)&t386))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t386) + 1, gm_srcdjp1iiokf034, 278, 17);
                                            gm_b0_1 t387 = GM_LD(gm_b0_1, (((uint8_t *)&t386) + 1));
                                        }
                                        goto ix371;
ie371:;
                                        {
                                            void * t389 = (void *)((uint8_t *)&F.l9);
                                            gm_t7te2tdpebbt4 t390;
                                            memset((uint8_t *)&t390 + 140, 0, 4);
                                            gm_tupk62cr6lbdu t391;
                                            gm_tq7s98ihonkju t392 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l24)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ke30i57o09o)));
                                            if (gm_unw.kind) {
                                                goto gm_ll360;
                                            }
                                            if (((((uint8_t *)&t392))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t392) + 8, gm_srcdjp1iiokf034, 280, 78);
                                            void * t393 = GM_LD(void *, (((uint8_t *)&t392) + 8));
                                            GM_ST(void *, (uint8_t *)&t391 + 0, t393);
                                            uint64_t t394 = gm_l47;
                                            uint32_t t395 = (uint32_t)t394;
                                            GM_ST(uint32_t, (uint8_t *)&t391 + 8, t395);
                                            gm_tl874t2atnlh3 t396 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t391 + 12, t396);
                                            gm_t2gtbh39ptekd t397;
                                            memset(&t397, 0, sizeof t397);
                                            GM_ST(uint8_t, ((uint8_t *)&t397 + 0), ((uint8_t)23ULL));
                                            GM_ST(gm_tupk62cr6lbdu, ((uint8_t *)&t397 + 8), t391);
                                            gm_tl874t2atnlh3 t398 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            gm_tc3oht4ksth9d t399;
                                            gm_f_synthetic_hdhcgpb((void *)&t399, &t397, GM_CONV(gm_cseahvg8v7p0vs, t398));
                                            gm_t22mtv3sd9prf t400;
                                            memset(&t400, 0, sizeof t400);
                                            GM_ST(uint8_t, ((uint8_t *)&t400 + 0), ((uint8_t)0ULL));
                                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t400 + 8), t399);
                                            GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t390 + 0, t400);
                                            gm_tl874t2atnlh3 t401 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t390 + 128, t401);
                                            gm_tftb1odc739t5 t402 = gm_f_push_jis5lhu(t389, &t390);
                                            if (((((uint8_t *)&t402))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t402) + 1, gm_srcdjp1iiokf034, 280, 17);
                                            gm_b0_1 t403 = GM_LD(gm_b0_1, (((uint8_t *)&t402) + 1));
                                        }
ix371:;
                                    }
                                    goto me367;
mn368:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 275, 9);
me367:;
                                }
lc356:;
                                {
                                    uint64_t t405 = gm_l46;
                                    uint64_t r406; bool o407 = __builtin_add_overflow(t405, ((uint64_t)1ULL), &r406);
                                    uint64_t t408 = r406;
                                    gm_l46 = t408;
                                }
                            }
gm_ll360:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb356; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc356; }
                            }
                            goto gm_ll41;
lb356:;
                        }
                    }
                    goto me48;
mn49:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 207, 5);
me48:;
                }
lc37:;
                {
                    uint64_t t410 = gm_l11;
                    uint64_t r411; bool o412 = __builtin_add_overflow(t410, ((uint64_t)1ULL), &r411);
                    uint64_t t413 = r411;
                    gm_l11 = t413;
                }
            }
gm_ll41:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb37; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc37; }
            }
            goto gm_lad;
lb37:;
        }
    }
    {
        gm_b16_8 t414 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t414);
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
__attribute__((aligned(4))) void gm_f_spread_parts_e62q2l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_spread_parts_e62q2l2 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* stmts */
    void * gm_l3; /* tys */
    void * gm_l4; /* v */
    gm_t88rcucg1b7uk gm_l5; /* vt */
    gm_b16_8 gm_l6; /* elems */
    gm_b16_8 gm_l7; /* names */
    gm_b16_8 gm_l8; /* es */
    gm_b16_8 gm_l9; /* ns */
    gm_tlcm4lvu3g1a8 gm_l10; /* a */
    void * gm_l11; /* base */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_th2lqbo77pvsc gm_l16; /* fs */
    gm_b16_8 gm_l17; /* f */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    void * gm_l23; /* base */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* k */
    gm_t88rcucg1b7uk gm_l28; /* t */
    void * gm_l29; /* proj */
    gm_th2lqbo77pvsc gm_l30; /* name */
    GM_STACK_CHECK(&F, 1937u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = gm_l1;
        gm_tkn42u7na3t81 t7;
        memset(&t7, 0, sizeof t7);
        void * t8 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 96);
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        void * t13 = gm_l4;
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 0));
        gm_t88rcucg1b7uk t15 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t10, t12, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t15;
    }
    {
        gm_b16_8 t16 = gm_f_empty_slice_rcd2jln();
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        gm_l6 = t17;
    }
    {
        gm_b16_8 t18 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        gm_l7 = t19;
    }
    {
        void * t20 = gm_l3;
        gm_t88rcucg1b7uk t21 = gm_l5;
        gm_t53at80pfdus3 t22;
        gm_f_kind_jcnt4hm((void *)&t22, t20, GM_CONV(gm_csguankko4lmhd, t21));
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        switch (t29) { case ((uint8_t)0ULL): goto ma27; case ((uint8_t)1ULL): goto ma27; case ((uint8_t)2ULL): goto ma27; case ((uint8_t)3ULL): goto ma27; case ((uint8_t)4ULL): goto ma27; case ((uint8_t)5ULL): goto ma27; case ((uint8_t)6ULL): goto ma27; case ((uint8_t)7ULL): goto ma27; case ((uint8_t)8ULL): goto ma27; case ((uint8_t)9ULL): goto ma27; case ((uint8_t)10ULL): goto ma27; case ((uint8_t)11ULL): goto ma26; case ((uint8_t)12ULL): goto ma24; case ((uint8_t)13ULL): goto ma27; case ((uint8_t)14ULL): goto ma27; case ((uint8_t)15ULL): goto ma27; case ((uint8_t)16ULL): goto ma27; case ((uint8_t)17ULL): goto ma27; case ((uint8_t)18ULL): goto ma27; case ((uint8_t)19ULL): goto ma27; case ((uint8_t)20ULL): goto ma26; case ((uint8_t)21ULL): goto ma27; case ((uint8_t)22ULL): goto ma27; case ((uint8_t)23ULL): goto ma27; case ((uint8_t)24ULL): goto ma27; case ((uint8_t)25ULL): goto ma27; case ((uint8_t)26ULL): goto ma25; default: goto ma27; }
ma24:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t31 != ((uint8_t)12ULL)) goto mn30;
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((((uint8_t *)&t22) + 8) + 0));
        gm_l8 = t32;
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((((uint8_t *)&t22) + 8) + 16));
        gm_l9 = t33;
        {
            gm_b16_8 t35 = gm_l8;
            gm_l6 = t35;
        }
        {
            gm_b16_8 t36 = gm_l9;
            gm_l7 = t36;
        }
        goto me23;
mn30:;
ma25:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t38 != ((uint8_t)26ULL)) goto mn37;
        gm_th2lqbo77pvsc t39;
        memset(&t39, 0, sizeof t39);
        uint8_t t40 = 0;
        F.ret = t39;
        goto gm_fret;
        goto me23;
mn37:;
ma26:;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t44 != ((uint8_t)11ULL)) goto pa43;
        goto po42;
pa43:;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t46 != ((uint8_t)20ULL)) goto pa45;
        goto po42;
pa45:;
        goto mn41;
po42:;
        {
            void * t48 = gm_l0;
            gm_t88rcucg1b7uk t49 = gm_l5;
            void * t50 = gm_l1;
            gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t50) + 72));
            gm_tb7u19loo3j87 t52;
            gm_f_spread_array_v7s332a((void *)&t52, (void *)NULL, cx, NULL, t48, t49, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tlcm4lvu3g1a8 t53;
            if (((((uint8_t *)&t52))[16] != 0)) {
                gm_tlcm4lvu3g1a8 t54 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t52) + 0));
                t53 = t54;
            } else {
                gm_th2lqbo77pvsc t55;
                memset(&t55, 0, sizeof t55);
                uint8_t t56 = 0;
                F.ret = t55;
                goto gm_fret;
            }
            gm_l10 = t53;
        }
        {
            void * t57 = gm_l0;
            void * t58 = gm_l4;
            void * t59 = gm_l2;
            void * t60 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t57, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t60;
        }
        {
            gm_f_list_j5ps4qc((void *)&F.l12, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 0));
                gm_l13 = t62;
            }
            {
                for (;;) {
                    uint64_t t64 = gm_l14;
                    uint64_t t65 = gm_l13;
                    uint8_t t66 = (t64 < t65);
                    if (!t66) goto lb63;
                    {
                        uint64_t t69 = gm_l14;
                        uint64_t t72 = GM_LD(uint64_t, ((uint8_t *)&t69));
                        gm_l15 = t72;
                        {
                            void * t74 = (void *)((uint8_t *)&F.l12);
                            gm_tpouv9r7lrri2 t75;
                            gm_th2lqbo77pvsc t76;
                            memset(&t76, 0, sizeof t76);
                            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t75 + 0, t76);
                            void * t77 = gm_l0;
                            void * t78 = GM_LD(void *, (((uint8_t *)t77) + 0));
                            void * t79 = gm_l11;
                            gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 8));
                            uint64_t t81 = gm_l15;
                            void * t82 = gm_l1;
                            gm_tl874t2atnlh3 t83 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t82) + 72));
                            void * t84 = gm_f_array_element_t6q5jtp((void *)NULL, cx, NULL, t78, t79, t80, t81, t83);
                            if (gm_unw.kind) {
                                goto gm_ll67;
                            }
                            GM_ST(void *, (uint8_t *)&t75 + 24, t84);
                            gm_tftb1odc739t5 t85 = gm_f_push_bsig5q3(t74, &t75);
                            if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 1, gm_srcdjp1iiokf034, 114, 33);
                            gm_b0_1 t86 = GM_LD(gm_b0_1, (((uint8_t *)&t85) + 1));
                        }
                        goto me70;
mn71:;
                        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 114, 13);
me70:;
                    }
lc63:;
                    {
                        uint64_t t88 = gm_l14;
                        uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                        uint64_t t91 = r89;
                        gm_l14 = t91;
                    }
                }
gm_ll67:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb63; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc63; }
                }
                goto gm_lad;
lb63:;
            }
        }
        {
            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t92);
            goto gm_fdone;
        }
        goto me23;
mn41:;
ma27:;
        {
            void * t95 = gm_l0;
            void * t96 = GM_LD(void *, (((uint8_t *)t95) + 0));
            gm_t88rcucg1b7uk t97 = gm_l5;
            gm_f_struct_fields_chqu3it((void *)&gm_l16, t96, GM_CONV(gm_csguankko4lmhd, t97));
        }
        {
            if (!((((uint8_t *)&gm_l16))[16] != 0)) goto ie98;
            gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
            gm_l17 = t99;
            void * t100 = gm_l0;
            gm_t88rcucg1b7uk t101 = gm_l5;
            gm_ttu09lij36f8c t102;
            gm_f_hidden_field_u7uao2s((void *)&t102, t100, GM_CONV(gm_csguankko4lmhd, t101));
            uint8_t t103;
            if (((((uint8_t *)&t102))[32] != 0)) goto mn105;
            t103 = ((uint8_t)1);
            goto me104;
mn105:;
            t103 = ((uint8_t)0);
            goto me104;
mn106:;
            gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 119, 28);
me104:;
            if (!t103) goto ie98;
            {
                gm_f_list_nof7vfj((void *)&F.l18, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_f_list_epvducg((void *)&F.l19, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t109 = gm_l17;
                    gm_l20 = t109;
                }
                {
                    gm_l21 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t111 = gm_l21;
                        uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        uint8_t t113 = (t111 < t112);
                        if (!t113) goto lb110;
                        {
                            uint64_t t116 = gm_l21;
                            uint64_t t117 = (uintptr_t)t116;
                            uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                            gm_t0mglksrkfa5e t121 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t117 * 40));
                            F.l22 = t121;
                            {
                                void * t123 = (void *)((uint8_t *)&F.l18);
                                gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l22) + 16));
                                gm_tftb1odc739t5 t125 = gm_f_push_k3l9nic(t123, GM_CONV(gm_csguankko4lmhd, t124));
                                if (((((uint8_t *)&t125))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t125) + 1, gm_srcdjp1iiokf034, 123, 21);
                                gm_b0_1 t126 = GM_LD(gm_b0_1, (((uint8_t *)&t125) + 1));
                            }
                            {
                                void * t127 = (void *)((uint8_t *)&F.l19);
                                gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                                gm_tftb1odc739t5 t129 = gm_f_push_3ef746b(t127, t128);
                                if (((((uint8_t *)&t129))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t129) + 1, gm_srcdjp1iiokf034, 124, 21);
                                gm_b0_1 t130 = GM_LD(gm_b0_1, (((uint8_t *)&t129) + 1));
                            }
                            goto me119;
mn120:;
                            gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 122, 17);
me119:;
                        }
lc110:;
                        {
                            uint64_t t132 = gm_l21;
                            uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                            uint64_t t135 = r133;
                            gm_l21 = t135;
                        }
                    }
lb110:;
                }
            }
            {
                gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
                gm_l6 = t137;
            }
            {
                gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&t138));
                gm_l7 = t139;
            }
            goto ix98;
ie98:;
            {
                void * t141 = gm_l0;
                void * t142 = gm_l1;
                gm_tl874t2atnlh3 t143 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t142) + 72));
                void * t144 = gm_l0;
                gm_t88rcucg1b7uk t145 = gm_l5;
                gm_b16_8 t146 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t144, t145);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t147;
                GM_ST(gm_b16_8, ((uint8_t *)&t147 + 0), t146);
                gm_tabb710d9mq2e t148;
                gm_f_format_7f8ekea((void *)&t148, (void *)NULL, cx, NULL, t147);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 8, gm_srcdjp1iiokf034, 129, 34);
                gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&t148) + 8));
                gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                gm_b0_1 t151 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t141, t143, t150);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_th2lqbo77pvsc t152;
                memset(&t152, 0, sizeof t152);
                uint8_t t153 = 0;
                F.ret = t152;
                goto gm_fret;
            }
ix98:;
        }
        goto me23;
mn93:;
ma28:;
        gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 104, 5);
me23:;
    }
    {
        void * t154 = gm_l0;
        void * t155 = gm_l4;
        void * t156 = gm_l2;
        void * t157 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t154, t155, t156);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l23 = t157;
    }
    {
        gm_f_list_j5ps4qc((void *)&F.l24, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t159 = gm_l6;
            gm_l25 = t159;
        }
        {
            gm_l26 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t161 = gm_l26;
                uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                uint8_t t163 = (t161 < t162);
                if (!t163) goto lb160;
                {
                    uint64_t t166 = gm_l26;
                    uint64_t t167 = gm_l26;
                    uint64_t t168 = (uintptr_t)t167;
                    uint64_t t169 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                    gm_t88rcucg1b7uk t170 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t168 * 4));
                    gm_tlcm4lvu3g1a8 t171;
                    GM_ST(uint32_t, (uint8_t *)&t171 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t171 + 0), t166);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t171 + 8), t170);
                    uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)&t171) + 0));
                    gm_l27 = t174;
                    gm_t88rcucg1b7uk t175 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t171) + 8));
                    gm_l28 = t175;
                    {
                        gm_t88rcucg1b7uk t177 = gm_l28;
                        void * t178 = gm_l23;
                        uint64_t t179 = gm_l27;
                        uint32_t t180 = (uint32_t)t179;
                        gm_tbl8il92l97ut t181;
                        GM_ST(uint32_t, (uint8_t *)&t181 + 12, 0);
                        GM_ST(void *, ((uint8_t *)&t181 + 0), t178);
                        GM_ST(uint32_t, ((uint8_t *)&t181 + 8), t180);
                        gm_teti411eujvf4 t182;
                        memset(&t182, 0, sizeof t182);
                        GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)19ULL));
                        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t182 + 8), t181);
                        void * t183 = gm_l1;
                        gm_tl874t2atnlh3 t184 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t183) + 72));
                        void * t185 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t177, &t182, t184);
                        if (gm_unw.kind) {
                            goto gm_ll164;
                        }
                        gm_l29 = t185;
                    }
                    {
                        gm_th2lqbo77pvsc t186;
                        uint64_t t188 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t189 = (t188 > ((uint64_t)0ULL));
                        if (!t189) goto ie187;
                        uint64_t t191 = gm_l27;
                        uint64_t t192 = (uintptr_t)t191;
                        uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        if (t192 >= (uintptr_t)(t193)) gm_trap_index(t192, t193, gm_srcdjp1iiokf034, 138, 44);
                        gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t192 * 16));
                        gm_b16_8 t195 = t194;
                        gm_th2lqbo77pvsc t196;
                        GM_ST(uint8_t, (uint8_t *)&t196 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t196 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t196 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t196), t195);
                        ((uint8_t *)&t196)[16] = 1;
                        t186 = t196;
                        goto ix187;
ie187:;
                        gm_th2lqbo77pvsc t198;
                        memset(&t198, 0, sizeof t198);
                        gm_th2lqbo77pvsc t199 = t198;
                        t186 = t199;
ix187:;
                        gm_l30 = t186;
                    }
                    {
                        void * t200 = (void *)((uint8_t *)&F.l24);
                        gm_tpouv9r7lrri2 t201;
                        memcpy((uint8_t *)&t201 + 0, ((uint8_t *)&gm_l30), 24);
                        void * t202 = gm_l29;
                        GM_ST(void *, (uint8_t *)&t201 + 24, t202);
                        gm_tftb1odc739t5 t203 = gm_f_push_bsig5q3(t200, &t201);
                        if (((((uint8_t *)&t203))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t203) + 1, gm_srcdjp1iiokf034, 139, 9);
                        gm_b0_1 t204 = GM_LD(gm_b0_1, (((uint8_t *)&t203) + 1));
                    }
                    goto me172;
mn173:;
                    gm_trap("no match arm matched the scrutinee", gm_srcdjp1iiokf034, 136, 5);
me172:;
                }
lc160:;
                {
                    uint64_t t206 = gm_l26;
                    uint64_t r207; bool o208 = __builtin_add_overflow(t206, ((uint64_t)1ULL), &r207);
                    uint64_t t209 = r207;
                    gm_l26 = t209;
                }
            }
gm_ll164:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb160; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc160; }
            }
            goto gm_lad;
lb160:;
        }
    }
    {
        gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t210);
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
extern __attribute__((aligned(4))) void gm_f_synthetic_hdhcgpb(void *gm_out, const void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tc3oht4ksth9d gm_f_synthetic_hdhcgpb_c(gm_t2gtbh39ptekd a0, gm_cseahvg8v7p0vs a1) { gm_tc3oht4ksth9d gm_r; gm_f_synthetic_hdhcgpb(&gm_r, &a0, a1); return gm_r; }
