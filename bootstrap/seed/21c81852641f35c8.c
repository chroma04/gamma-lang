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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t a[832]; } gm_tsoqes8js6nm3;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_b16_8 a[7]; } gm_tdv3dkjvo1l7t;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tipro5npl6rd8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsoqes8js6nm3) == 832 && _Alignof(gm_tsoqes8js6nm3) == 1 && offsetof(gm_tsoqes8js6nm3, a) == 0, "layout of `[832]u8`");
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tdv3dkjvo1l7t) == 112 && _Alignof(gm_tdv3dkjvo1l7t) == 8 && offsetof(gm_tdv3dkjvo1l7t, a) == 0, "layout of `[7]string`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tipro5npl6rd8) == 40 && _Alignof(gm_tipro5npl6rd8) == 8 && offsetof(gm_tipro5npl6rd8, f0) == 0 && offsetof(gm_tipro5npl6rd8, f1) == 4 && offsetof(gm_tipro5npl6rd8, f2) == 8 && offsetof(gm_tipro5npl6rd8, f3) == 24, "layout of `Ran`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; } gm_tji6flc1b40vj;
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tji6flc1b40vj) == 56 && _Alignof(gm_tji6flc1b40vj) == 8 && offsetof(gm_tji6flc1b40vj, f0) == 0 && offsetof(gm_tji6flc1b40vj, f1) == 16 && offsetof(gm_tji6flc1b40vj, f2) == 32, "layout of `TreeEntry`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
struct gm_fr_check_link_chains_sttldqn {
    gm_frame h;
    gm_tji6flc1b40vj l3; /* e */
    gm_trqfvmjtt4sij l5; /* dir */
    gm_tl572d7579hns ret;
    gm_b0_1 w1;
};
struct gm_fr_check_link_glakqpq {
    gm_frame h;
    uint64_t l3; /* depth */
    gm_tl572d7579hns ret;
    gm_b0_1 w0;
};
struct gm_fr_check_triple_uqaropr {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* parts */
    gm_t0rfe1ou14h21 l9; /* tg */
    gm_tl572d7579hns ret;
    gm_b0_1 w0;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_count_9fkti0l {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_decimal_bits_c63p8nv {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_eq_ignore_case_qqsqlkn {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fields_ojgi3et {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_find_nab29co {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_for_body_1avb45k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_1vrv15m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_25s43ua {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_5dma72c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_6l4llq8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_e61ld42 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_g303nmp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_hb9k0f9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_hrv26t6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_i2koblr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_jvjn1fs {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* parts */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_for_body_tbgr2dj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_tv3u4f0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_ukps8ob {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* parts */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_for_body_v43n3ub {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_space_uen38t5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_upper_iekrftp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_sfknljb {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_leaves_directory_kq5llq4 {
    gm_frame h;
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_left_shift_77i68r4 {
    gm_frame h;
    gm_tsoqes8js6nm3 l2; /* scratch */
    gm_b0_1 ret;
};
struct gm_fr_lines_9uns0k2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_matches_at_jjpfr5u {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_path_less_il538gm {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* x */
    gm_trqfvmjtt4sij l4; /* y */
    uint8_t ret;
    gm_b0_1 w0;
    gm_b0_1 w1;
};
struct gm_fr_path_names_j9i34l1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* names */
    gm_trqfvmjtt4sij ret;
    gm_b0_1 w0;
};
struct gm_fr_program_on_path_fm43kjf {
    gm_frame h;
    gm_b16_8 l0; /* prog */
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_replace_alloc_t7hndlq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_replace_lf6hcmn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rfind_ai5s2js {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_right_shift_22mtui8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_rounded_integer_f0bb6k4 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_shift_3amrqim {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_should_round_up_s6nkn8b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_split_obtinjv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_split_once_mqcu1ce {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_strip_prefix_b4949s8 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_strip_suffix_8155gbi {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_to_lower_0fu6efk {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_trim_decimal_56e5p3i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_trim_end_6sjv6rg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_trim_start_179crak {
    gm_frame h;
    gm_b16_8 ret;
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
struct gm_fr_words_eeicdvm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij ret;
    gm_b0_1 w0;
};
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_count_9fkti0l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_decimal_bits_c63p8nv(void * a0, uint32_t a1, uint32_t a2, int64_t a3);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_eq_ignore_case_qqsqlkn(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fields_ojgi3et(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fields_ojgi3et_sfcmm6t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fields_ojgi3et_sq5308c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1063u09(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1avb45k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1vrv15m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_25s43ua(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_3400t3p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_3msbccb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_3q46euu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_5dma72c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_69vpqmq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6ji90d8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6l4llq8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_8edr2np(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_8jjs01b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_cbj68cn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_e61ld42(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fe8qj55(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g303nmp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g88hj6k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g9kn84c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_gp0er0a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hb9k0f9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hrv26t6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2koblr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ig27ig2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_j11vulv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_jvjn1fs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_keqa9hq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_kocuepv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_m8t719h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_n9765ef(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tbgr2dj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tdd1e60(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tv3u4f0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ui0bpsf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ui39jfe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ukps8ob(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_v43n3ub(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vetgq3s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vgf3305(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vn00ddp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_uen38t5(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_left_shift_77i68r4(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s1f7rak(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s1hv6k0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s30klli(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s84ppfm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s9kjq5m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sa157kc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_samvo8i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_scfak7n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sersjf1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sfohpq4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sld76pa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_snhh6ss(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_snvhnj5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sp8kip0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sqdm19s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sssrp43(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_svejqbu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_matches_at_jjpfr5u(gm_b16_8 a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_replace_alloc_t7hndlq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_replace_lf6hcmn(void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rfind_ai5s2js(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_right_shift_22mtui8(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint64_t gm_f_rounded_integer_f0bb6k4(void * a0);
__attribute__((aligned(4))) void gm_f_shift_3amrqim(void * a0, int64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_should_round_up_s6nkn8b(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s0v7938(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s1om374(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s3958ap(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s44sj5s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s5hgl7n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s76o7tg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s7d377q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s8m2rsv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sbt8562(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sf8feic(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_shbdnkf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sq2k3vc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sq3203i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_srei23l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_st26gm6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_st80pr1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_stnipdi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sumnih7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sut5g99(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_svmmoet(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_svn701p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_ttu09lij36f8c gm_f_split_once_mqcu1ce_c(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_strip_prefix_b4949s8_c(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_suffix_8155gbi(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_strip_suffix_8155gbi_c(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_to_lower_0fu6efk(uint8_t a0);
__attribute__((aligned(4))) void gm_f_trim_decimal_56e5p3i(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
GM_NOINLINE bool gm_pf1m4b8q4e78(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf70j0q3sgt1(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_chv3533b78v[72] __attribute__((aligned(8)));
extern uint8_t gm_k6oa34t4v6n[40] __attribute__((aligned(8)));
extern uint8_t gm_kh31phsjqu4[40] __attribute__((aligned(8)));
extern const uint8_t gm_s6hiq18oa7ku0[32];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_spgsforhruitc[30];
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src3s76fqo1a9jk[];
extern const char gm_src4mnsbede6fos[];
extern const char gm_srcbq9k9kvdokbf[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgr7vbgb5cqmg[];
extern const char gm_srchhdunc42909u[];
extern const char gm_srci5ni72s6oj5f[];
extern const char gm_srcmm27gepldtcm[];
extern const char gm_srcn623bb2dv5no[];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_find_nab29co(t1, t2);
    gm_tf5srh4mp4b3i t4 = t3;
    uint8_t t5;
    if (((((uint8_t *)&t4))[8] != 0)) goto mn7;
    t5 = ((uint8_t)0);
    goto me6;
mn7:;
    t5 = ((uint8_t)1);
    goto me6;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 82, 50);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1avb45k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_1avb45k F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 50u);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1vrv15m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_1vrv15m F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_path_less_il538gm *)((&F.h)->up))->l2);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcgr7vbgb5cqmg, 185, 34);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 185, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_25s43ua(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_25s43ua F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* dir */
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t6 = gm_l1;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_program_on_path_fm43kjf *)((&F.h)->up))->l0));
        gm_b16_8 t8 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t9 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t9) goto ie5;
        {
            uint8_t t11 = 0;
            ((struct gm_fr_program_on_path_fm43kjf *)((&F.h)->up))->ret = ((uint8_t)1);
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t11;
            goto gm_lad;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_b0_1 t12;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srchhdunc42909u, 114, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_5dma72c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_5dma72c F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t7 = (t6 > ((uint64_t)0ULL));
        if (!t7) goto ie5;
        uint8_t t8 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sl3q52ep6aa53, 1) == 0));
        if (!t8) goto ie5;
        {
            void * t10 = (void *)((uint8_t *)&((struct gm_fr_path_names_j9i34l1 *)((&F.h)->up))->l1);
            gm_b16_8 t11 = gm_l1;
            gm_tftb1odc739t5 t12 = gm_f_push_3ef746b(t10, t11);
            gm_tftb1odc739t5 t13 = t12;
            if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcbq9k9kvdokbf, 257, 41);
            gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 256, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6l4llq8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_6l4llq8 F;
    gm_b16_8 gm_l0; /* <item> */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_check_link_glakqpq *)((&F.h)->up))->l3));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcbq9k9kvdokbf, 274, 36);
        uint64_t t7 = r5;
        GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_check_link_glakqpq *)((&F.h)->up))->l3), t7);
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 274, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_e61ld42(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_e61ld42 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* w */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_words_eeicdvm *)((&F.h)->up))->l1);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcn623bb2dv5no, 302, 37);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcn623bb2dv5no, 302, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g303nmp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_g303nmp F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_check_link_chains_sttldqn *)((&F.h)->up))->l5);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcbq9k9kvdokbf, 295, 42);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 295, 9);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hb9k0f9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_hb9k0f9 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_for_body_jvjn1fs *)((&F.h)->up))->l2);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcbq9k9kvdokbf, 139, 42);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 139, 9);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hrv26t6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_hrv26t6 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t5 = gm_l1;
        gm_b16_8 t6 = t5;
        if (gm_pf1m4b8q4e78(cx, gm_som0q53jqvjh3, 4, gm_src4mnsbede6fos, 57, 36)) {
            goto gm_lad;
        }
        if (gm_pf70j0q3sgt1(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), gm_src4mnsbede6fos, 57, 36)) {
            goto gm_lad;
        }
        if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_src4mnsbede6fos, 57, 36)) {
            goto gm_lad;
        }
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_src4mnsbede6fos, 57, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2koblr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_i2koblr F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_check_triple_uqaropr *)((&F.h)->up))->l2);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcmm27gepldtcm, 257, 39);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 257, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tbgr2dj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_tbgr2dj F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_path_less_il538gm *)((&F.h)->up))->l4);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcgr7vbgb5cqmg, 187, 34);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 187, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_v43n3ub(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_v43n3ub F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_for_body_ukps8ob *)((&F.h)->up))->l2);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcbq9k9kvdokbf, 173, 42);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 173, 9);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_tcuv9rv F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 213u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 234, 13);
        gm_tih6h4avv5nrb t4;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[5])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tih6h4avv5nrb t5 = t4;
        gm_t5j14m8ru132p t6;
        if (!((((uint8_t *)&t5))[0] != 0)) {
            gm_t5j14m8ru132p t7 = GM_LD(gm_t5j14m8ru132p, (((uint8_t *)&t5) + 8));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        uint8_t t9 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8) + 0) == ((uint8_t)0ULL)));
        uint8_t t10 = 0;
        F.ret = t9;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_uen38t5(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_space_uen38t5 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 20u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint8_t t3 = 0;
    uint8_t t4 = 0;
    uint8_t t5 = 0;
    uint8_t t6 = gm_l0;
    uint8_t t7 = ((t6 == ((uint8_t)32ULL)));
    if (!t7) {
        uint8_t t8 = gm_l0;
        uint8_t t9 = ((t8 == ((uint8_t)9ULL)));
        t5 = t9;
    } else t5 = 1;
    if (!t5) {
        uint8_t t10 = gm_l0;
        uint8_t t11 = ((t10 == ((uint8_t)10ULL)));
        t4 = t11;
    } else t4 = 1;
    if (!t4) {
        uint8_t t12 = gm_l0;
        uint8_t t13 = ((t12 == ((uint8_t)13ULL)));
        t3 = t13;
    } else t3 = 1;
    if (!t3) {
        uint8_t t14 = gm_l0;
        uint8_t t15 = ((t14 == ((uint8_t)11ULL)));
        t2 = t15;
    } else t2 = 1;
    if (!t2) {
        uint8_t t16 = gm_l0;
        uint8_t t17 = ((t16 == ((uint8_t)12ULL)));
        t1 = t17;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_upper_iekrftp F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)65ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)90ULL));
        t1 = t5;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_to_lower_0fu6efk(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_lower_0fu6efk F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1;
    uint8_t t3 = gm_l0;
    uint8_t t4 = gm_f_is_upper_iekrftp(t3);
    if (!t4) goto ie2;
    uint8_t t6 = gm_l0;
    uint8_t r7; bool o8 = __builtin_add_overflow(t6, ((uint8_t)32ULL), &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_srci5ni72s6oj5f, 42, 53);
    uint8_t t9 = r7;
    uint8_t t10 = t9;
    t1 = t10;
    goto ix2;
ie2:;
    uint8_t t12 = gm_l0;
    uint8_t t13 = t12;
    t1 = t13;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_f_trim_start_179crak(t1);
    gm_b16_8 t3 = gm_f_trim_end_6sjv6rg(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint64_t gm_f_count_9fkti0l(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_count_9fkti0l F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* rest */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 322u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_spgsforhruitc));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(29));
            gm_b16_8 t7 = t6;
            gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t7) + 8), gm_src3d0v42kavhv7, 100, 26);
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        gm_b16_8 t8 = gm_l0;
        gm_l3 = t8;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb9;
            {
                gm_b16_8 t12 = gm_l3;
                gm_b16_8 t13 = gm_l1;
                gm_tf5srh4mp4b3i t14 = gm_f_find_nab29co(t12, t13);
                gm_tf5srh4mp4b3i t15 = t14;
                uint64_t t16;
                if (((((uint8_t *)&t15))[8] != 0)) {
                    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
                    t16 = t17;
                } else {
                    goto lb9;
                }
                gm_l4 = t16;
            }
            {
                uint64_t t18 = gm_l2;
                uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
                if (o20) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 105, 9);
                uint64_t t21 = r19;
                gm_l2 = t21;
            }
            {
                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                void * t23 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                uint64_t t24 = gm_l4;
                uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint64_t r26; bool o27 = __builtin_add_overflow(t24, t25, &r26);
                if (o27) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 106, 21);
                uint64_t t28 = r26;
                uint64_t t29 = (uintptr_t)t28;
                uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint64_t t31 = (uintptr_t)t30;
                if (t29 > t31 || t31 > (uintptr_t)t22) gm_trap_slice(t29, t31, t22, gm_src3d0v42kavhv7, 106, 16);
                gm_b16_8 t32;
                GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t23 + t29 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t29));
                gm_l3 = t32;
            }
lc9:;
        }
lb9:;
    }
    {
        uint64_t t33 = gm_l2;
        uint8_t t34 = 0;
        F.ret = t33;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint64_t gm_f_decimal_bits_c63p8nv(void * a0, uint32_t a1, uint32_t a2, int64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decimal_bits_c63p8nv F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* mant_bits */
    uint32_t gm_l2; /* exp_bits */
    int64_t gm_l3; /* bias */
    int64_t gm_l4; /* emax */
    uint64_t gm_l5; /* inf */
    int64_t gm_l6; /* exp */
    int64_t gm_l7; /* n */
    int64_t gm_l8; /* n */
    int64_t gm_l9; /* n */
    uint64_t gm_l10; /* mant */
    GM_STACK_CHECK_SHALLOW(&F, 923u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l2;
        unsigned __int128 n3 = (unsigned __int128)t2;
        if (n3 >= 64) gm_trap_shift((uint64_t)n3, "i64", gm_src3d0v42kavhv7, 554, 14);
        int64_t r4 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n3);
        bool o5 = (int64_t)(r4 >> (int)n3) != ((int64_t)1LL);
        if (o5) gm_trap("integer overflow in `<<`", gm_src3d0v42kavhv7, 554, 14);
        int64_t t6 = r4;
        int64_t r7; bool o8 = __builtin_sub_overflow(t6, ((int64_t)1LL), &r7);
        if (o8) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 554, 13);
        int64_t t9 = r7;
        gm_l4 = t9;
    }
    {
        int64_t t10 = gm_l4;
        uint64_t t11 = (uint64_t)t10;
        uint32_t t12 = gm_l1;
        unsigned __int128 n13 = (unsigned __int128)t12;
        if (n13 >= 64) gm_trap_shift((uint64_t)n13, "u64", gm_src3d0v42kavhv7, 555, 16);
        uint64_t r14 = (uint64_t)((uint64_t)t11 << (int)n13);
        bool o15 = (uint64_t)(r14 >> (int)n13) != t11;
        if (o15) gm_trap("integer overflow in `<<`", gm_src3d0v42kavhv7, 555, 16);
        uint64_t t16 = r14;
        gm_l5 = t16;
    }
    {
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 800));
        uint8_t t20 = ((t19 == ((uint64_t)0ULL)));
        if (!t20) goto ie17;
        {
            uint8_t t22 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        void * t24 = gm_l0;
        int64_t t25 = GM_LD(int64_t, (((uint8_t *)t24) + 808));
        uint8_t t26 = (t25 > ((int64_t)310LL));
        if (!t26) goto ie23;
        {
            uint64_t t28 = gm_l5;
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t31 = gm_l0;
        int64_t t32 = GM_LD(int64_t, (((uint8_t *)t31) + 808));
        uint8_t t33 = (t32 < ((int64_t)-330LL));
        if (!t33) goto ie30;
        {
            uint8_t t35 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        gm_l6 = ((int64_t)0LL);
    }
    {
        for (;;) {
            void * t37 = gm_l0;
            int64_t t38 = GM_LD(int64_t, (((uint8_t *)t37) + 808));
            uint8_t t39 = (t38 > ((int64_t)0LL));
            if (!t39) goto lb36;
            {
                gm_l7 = ((int64_t)27LL);
            }
            {
                void * t43 = gm_l0;
                int64_t t44 = GM_LD(int64_t, (((uint8_t *)t43) + 808));
                uint8_t t45 = (t44 < ((int64_t)9LL));
                if (!t45) goto ie42;
                {
                    uint8_t *t47 = (uint8_t *)(((uint8_t *)gm_chv3533b78v));
                    void * t48 = gm_l0;
                    int64_t t49 = GM_LD(int64_t, (((uint8_t *)t48) + 808));
                    uint64_t t50 = (uint64_t)t49;
                    uint64_t t51 = (uintptr_t)t50;
                    if (t51 >= (uintptr_t)(9)) gm_trap_index(t51, 9, gm_src3d0v42kavhv7, 563, 27);
                    int64_t t52 = GM_LD(int64_t, (t47 + t51 * 8));
                    gm_l7 = t52;
                }
                goto ix42;
ie42:;
ix42:;
            }
            {
                void * t53 = gm_l0;
                int64_t t54 = gm_l7;
                if (t54 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 564, 18);
                int64_t t55 = (int64_t)((uint64_t)0 - (uint64_t)t54);
                gm_f_shift_3amrqim(t53, t55);
            }
            {
                int64_t t56 = gm_l6;
                int64_t t57 = gm_l7;
                int64_t r58; bool o59 = __builtin_add_overflow(t56, t57, &r58);
                if (o59) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 565, 9);
                int64_t t60 = r58;
                gm_l6 = t60;
            }
lc36:;
        }
lb36:;
    }
    {
        for (;;) {
            uint8_t t62 = 0;
            void * t63 = gm_l0;
            int64_t t64 = GM_LD(int64_t, (((uint8_t *)t63) + 808));
            uint8_t t65 = (t64 < ((int64_t)0LL));
            if (!t65) {
                uint8_t t66 = 0;
                void * t67 = gm_l0;
                int64_t t68 = GM_LD(int64_t, (((uint8_t *)t67) + 808));
                uint8_t t69 = ((t68 == ((int64_t)0LL)));
                if (t69) {
                    uint64_t t70 = (uintptr_t)((uint64_t)0ULL);
                    void * t71 = gm_l0;
                    if (t70 >= (uintptr_t)(800)) gm_trap_index(t70, 800, gm_src3d0v42kavhv7, 567, 38);
                    uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)t71) + 0) + t70 * 1));
                    uint8_t t73 = (t72 < ((uint8_t)5ULL));
                    t66 = t73;
                } else t66 = 0;
                t62 = t66;
            } else t62 = 1;
            if (!t62) goto lb61;
            {
                gm_l8 = ((int64_t)27LL);
            }
            {
                void * t77 = gm_l0;
                int64_t t78 = GM_LD(int64_t, (((uint8_t *)t77) + 808));
                if (t78 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 569, 12);
                int64_t t79 = (int64_t)((uint64_t)0 - (uint64_t)t78);
                uint8_t t80 = (t79 < ((int64_t)9LL));
                if (!t80) goto ie76;
                {
                    uint8_t *t82 = (uint8_t *)(((uint8_t *)gm_chv3533b78v));
                    void * t83 = gm_l0;
                    int64_t t84 = GM_LD(int64_t, (((uint8_t *)t83) + 808));
                    if (t84 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 569, 36);
                    int64_t t85 = (int64_t)((uint64_t)0 - (uint64_t)t84);
                    uint64_t t86 = (uint64_t)t85;
                    uint64_t t87 = (uintptr_t)t86;
                    if (t87 >= (uintptr_t)(9)) gm_trap_index(t87, 9, gm_src3d0v42kavhv7, 569, 28);
                    int64_t t88 = GM_LD(int64_t, (t82 + t87 * 8));
                    gm_l8 = t88;
                }
                goto ix76;
ie76:;
ix76:;
            }
            {
                void * t89 = gm_l0;
                int64_t t90 = gm_l8;
                gm_f_shift_3amrqim(t89, t90);
            }
            {
                int64_t t91 = gm_l6;
                int64_t t92 = gm_l8;
                int64_t r93; bool o94 = __builtin_sub_overflow(t91, t92, &r93);
                if (o94) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 571, 9);
                int64_t t95 = r93;
                gm_l6 = t95;
            }
lc61:;
        }
lb61:;
    }
    {
        int64_t t96 = gm_l6;
        int64_t r97; bool o98 = __builtin_sub_overflow(t96, ((int64_t)1LL), &r97);
        if (o98) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 574, 5);
        int64_t t99 = r97;
        gm_l6 = t99;
    }
    {
        int64_t t101 = gm_l6;
        int64_t t102 = gm_l3;
        int64_t r103; bool o104 = __builtin_add_overflow(t102, ((int64_t)1LL), &r103);
        if (o104) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 576, 14);
        int64_t t105 = r103;
        uint8_t t106 = (t101 < t105);
        if (!t106) goto ie100;
        {
            int64_t t108 = gm_l3;
            int64_t r109; bool o110 = __builtin_add_overflow(t108, ((int64_t)1LL), &r109);
            if (o110) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 577, 14);
            int64_t t111 = r109;
            int64_t t112 = gm_l6;
            int64_t r113; bool o114 = __builtin_sub_overflow(t111, t112, &r113);
            if (o114) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 577, 14);
            int64_t t115 = r113;
            gm_l9 = t115;
        }
        {
            void * t116 = gm_l0;
            int64_t t117 = gm_l9;
            if (t117 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 578, 18);
            int64_t t118 = (int64_t)((uint64_t)0 - (uint64_t)t117);
            gm_f_shift_3amrqim(t116, t118);
        }
        {
            int64_t t119 = gm_l6;
            int64_t t120 = gm_l9;
            int64_t r121; bool o122 = __builtin_add_overflow(t119, t120, &r121);
            if (o122) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 579, 9);
            int64_t t123 = r121;
            gm_l6 = t123;
        }
        goto ix100;
ie100:;
ix100:;
    }
    {
        int64_t t125 = gm_l6;
        int64_t t126 = gm_l3;
        int64_t r127; bool o128 = __builtin_sub_overflow(t125, t126, &r127);
        if (o128) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 581, 8);
        int64_t t129 = r127;
        int64_t t130 = gm_l4;
        uint8_t t131 = (t129 >= t130);
        if (!t131) goto ie124;
        {
            uint64_t t133 = gm_l5;
            uint8_t t134 = 0;
            F.ret = t133;
            goto gm_fret;
        }
        goto ix124;
ie124:;
ix124:;
    }
    {
        void * t135 = gm_l0;
        uint32_t t136 = gm_l1;
        int64_t t137 = (int64_t)t136;
        int64_t r138; bool o139 = __builtin_add_overflow(((int64_t)1LL), t137, &r138);
        if (o139) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 583, 14);
        int64_t t140 = r138;
        gm_f_shift_3amrqim(t135, t140);
    }
    {
        void * t141 = gm_l0;
        uint64_t t142 = gm_f_rounded_integer_f0bb6k4(t141);
        gm_l10 = t142;
    }
    {
        uint64_t t144 = gm_l10;
        uint32_t t145 = gm_l1;
        unsigned __int128 n146 = (unsigned __int128)t145;
        if (n146 >= 64) gm_trap_shift((uint64_t)n146, "u64", gm_src3d0v42kavhv7, 585, 16);
        uint64_t r147 = (uint64_t)((uint64_t)((uint64_t)2ULL) << (int)n146);
        bool o148 = (uint64_t)(r147 >> (int)n146) != ((uint64_t)2ULL);
        if (o148) gm_trap("integer overflow in `<<`", gm_src3d0v42kavhv7, 585, 16);
        uint64_t t149 = r147;
        uint8_t t150 = ((t144 == t149));
        if (!t150) goto ie143;
        {
            uint64_t t152 = gm_l10;
            unsigned __int128 n153 = (unsigned __int128)((uint64_t)1ULL);
            if (n153 >= 64) gm_trap_shift((uint64_t)n153, "u64", gm_src3d0v42kavhv7, 587, 9);
            uint64_t t154 = (uint64_t)(t152 >> (int)n153);
            gm_l10 = t154;
        }
        {
            int64_t t155 = gm_l6;
            int64_t r156; bool o157 = __builtin_add_overflow(t155, ((int64_t)1LL), &r156);
            if (o157) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 588, 9);
            int64_t t158 = r156;
            gm_l6 = t158;
        }
        {
            int64_t t160 = gm_l6;
            int64_t t161 = gm_l3;
            int64_t r162; bool o163 = __builtin_sub_overflow(t160, t161, &r162);
            if (o163) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 589, 12);
            int64_t t164 = r162;
            int64_t t165 = gm_l4;
            uint8_t t166 = (t164 >= t165);
            if (!t166) goto ie159;
            {
                uint64_t t168 = gm_l5;
                uint8_t t169 = 0;
                F.ret = t168;
                goto gm_fret;
            }
            goto ix159;
ie159:;
ix159:;
        }
        goto ix143;
ie143:;
ix143:;
    }
    {
        uint64_t t171 = gm_l10;
        uint32_t t172 = gm_l1;
        unsigned __int128 n173 = (unsigned __int128)t172;
        if (n173 >= 64) gm_trap_shift((uint64_t)n173, "u64", gm_src3d0v42kavhv7, 592, 16);
        uint64_t r174 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n173);
        bool o175 = (uint64_t)(r174 >> (int)n173) != ((uint64_t)1ULL);
        if (o175) gm_trap("integer overflow in `<<`", gm_src3d0v42kavhv7, 592, 16);
        uint64_t t176 = r174;
        uint64_t t177 = (uint64_t)(t171 & t176);
        uint8_t t178 = ((t177 == ((uint64_t)0ULL)));
        if (!t178) goto ie170;
        {
            int64_t t180 = gm_l3;
            gm_l6 = t180;
        }
        goto ix170;
ie170:;
ix170:;
    }
    {
        uint64_t t181 = gm_l10;
        uint32_t t182 = gm_l1;
        unsigned __int128 n183 = (unsigned __int128)t182;
        if (n183 >= 64) gm_trap_shift((uint64_t)n183, "u64", gm_src3d0v42kavhv7, 593, 22);
        uint64_t r184 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n183);
        bool o185 = (uint64_t)(r184 >> (int)n183) != ((uint64_t)1ULL);
        if (o185) gm_trap("integer overflow in `<<`", gm_src3d0v42kavhv7, 593, 22);
        uint64_t t186 = r184;
        uint64_t r187; bool o188 = __builtin_sub_overflow(t186, ((uint64_t)1ULL), &r187);
        if (o188) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 593, 21);
        uint64_t t189 = r187;
        uint64_t t190 = (uint64_t)(t181 & t189);
        int64_t t191 = gm_l6;
        int64_t t192 = gm_l3;
        int64_t r193; bool o194 = __builtin_sub_overflow(t191, t192, &r193);
        if (o194) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 593, 53);
        int64_t t195 = r193;
        uint64_t t196 = (uint64_t)t195;
        uint32_t t197 = gm_l1;
        unsigned __int128 n198 = (unsigned __int128)t197;
        if (n198 >= 64) gm_trap_shift((uint64_t)n198, "u64", gm_src3d0v42kavhv7, 593, 52);
        uint64_t r199 = (uint64_t)((uint64_t)t196 << (int)n198);
        bool o200 = (uint64_t)(r199 >> (int)n198) != t196;
        if (o200) gm_trap("integer overflow in `<<`", gm_src3d0v42kavhv7, 593, 52);
        uint64_t t201 = r199;
        uint64_t t202 = (uint64_t)(t190 | t201);
        uint8_t t203 = 0;
        F.ret = t202;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_eq_ignore_case_qqsqlkn(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_ignore_case_qqsqlkn F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t5 = (!(t3 == t4));
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l2 = t9;
        }
        {
            for (;;) {
                uint64_t t11 = gm_l3;
                uint64_t t12 = gm_l2;
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l3;
                    uint64_t t17 = t16;
                    uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&t17));
                    gm_l4 = t20;
                    {
                        uint64_t t23 = gm_l4;
                        uint64_t t24 = (uintptr_t)t23;
                        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_src3d0v42kavhv7, 232, 27);
                        uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t24 * 1));
                        uint8_t t27 = gm_f_to_lower_0fu6efk(t26);
                        uint64_t t28 = gm_l4;
                        uint64_t t29 = (uintptr_t)t28;
                        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t29 >= (uintptr_t)(t30)) gm_trap_index(t29, t30, gm_src3d0v42kavhv7, 232, 55);
                        uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t29 * 1));
                        uint8_t t32 = gm_f_to_lower_0fu6efk(t31);
                        uint8_t t33 = (!(t27 == t32));
                        if (!t33) goto ie22;
                        {
                            uint8_t t35 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 231, 5);
me18:;
                }
lc10:;
                {
                    uint64_t t37 = gm_l3;
                    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                    uint64_t t40 = r38;
                    gm_l3 = t40;
                }
            }
lb10:;
        }
    }
    {
        uint8_t t41 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fields_ojgi3et(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fields_ojgi3et F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* start */
    GM_STACK_CHECK(&F, 241u);
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
                for (;;) {
                    uint64_t t6 = gm_l1;
                    uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t8 = (t6 < t7);
                    if (!t8) goto lb5;
                    uint64_t t9 = gm_l1;
                    uint64_t t10 = (uintptr_t)t9;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_src3d0v42kavhv7, 216, 44);
                    uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t10 * 1));
                    uint8_t t13 = gm_f_is_space_uen38t5(t12);
                    if (!t13) goto lb5;
                    {
                        uint64_t t16 = gm_l1;
                        uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
                        if (o18) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 216, 52);
                        uint64_t t19 = r17;
                        gm_l1 = t19;
                    }
lc5:;
                }
lb5:;
            }
            {
                uint64_t t21 = gm_l1;
                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t23 = (t21 >= t22);
                if (!t23) goto ie20;
                {
                    gm_b0_1 t25;
                    uint8_t t26 = 0;
                    F.ret = t25;
                    goto gm_fret;
                }
                goto ix20;
ie20:;
ix20:;
            }
            {
                uint64_t t27 = gm_l1;
                gm_l2 = t27;
            }
            {
                for (;;) {
                    uint64_t t29 = gm_l1;
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t31 = (t29 < t30);
                    if (!t31) goto lb28;
                    uint64_t t32 = gm_l1;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 219, 48);
                    uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                    uint8_t t36 = gm_f_is_space_uen38t5(t35);
                    uint8_t t37 = (uint8_t)!t36;
                    if (!t37) goto lb28;
                    {
                        uint64_t t40 = gm_l1;
                        uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                        if (o42) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 219, 56);
                        uint64_t t43 = r41;
                        gm_l1 = t43;
                    }
lc28:;
                }
lb28:;
            }
            {
                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t45 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t46 = gm_l2;
                uint64_t t47 = (uintptr_t)t46;
                uint64_t t48 = gm_l1;
                uint64_t t49 = (uintptr_t)t48;
                if (t47 > t49 || t49 > (uintptr_t)t44) gm_trap_slice(t47, t49, t44, gm_src3d0v42kavhv7, 220, 15);
                gm_b16_8 t50;
                GM_ST(void *, (uint8_t *)&t50, (void *)(((uint8_t *)t45 + t47 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(t49 - t47));
                gm_node *pn51 = gm_find(cx, 993381376);
                if (!pn51) gm_trap("no handler installed for effect `Yield`", gm_src3d0v42kavhv7, 220, 9);
                gm_b0_1 t52;
                t52 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn51->clauses[0])(pn51->env, pn51->hctx, pn51, t50);
                if (gm_unw.kind) {
                    goto gm_ll3;
                }
            }
lc2:;
        }
gm_ll3:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fields_ojgi3et_sfcmm6t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fields_ojgi3et F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* start */
    GM_STACK_CHECK(&F, 241u);
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
                for (;;) {
                    uint64_t t6 = gm_l1;
                    uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t8 = (t6 < t7);
                    if (!t8) goto lb5;
                    uint64_t t9 = gm_l1;
                    uint64_t t10 = (uintptr_t)t9;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_src3d0v42kavhv7, 216, 44);
                    uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t10 * 1));
                    uint8_t t13 = gm_f_is_space_uen38t5(t12);
                    if (!t13) goto lb5;
                    {
                        uint64_t t16 = gm_l1;
                        uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
                        if (o18) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 216, 52);
                        uint64_t t19 = r17;
                        gm_l1 = t19;
                    }
lc5:;
                }
lb5:;
            }
            {
                uint64_t t21 = gm_l1;
                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t23 = (t21 >= t22);
                if (!t23) goto ie20;
                {
                    gm_b0_1 t25;
                    uint8_t t26 = 0;
                    F.ret = t25;
                    goto gm_fret;
                }
                goto ix20;
ie20:;
ix20:;
            }
            {
                uint64_t t27 = gm_l1;
                gm_l2 = t27;
            }
            {
                for (;;) {
                    uint64_t t29 = gm_l1;
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t31 = (t29 < t30);
                    if (!t31) goto lb28;
                    uint64_t t32 = gm_l1;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 219, 48);
                    uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                    uint8_t t36 = gm_f_is_space_uen38t5(t35);
                    uint8_t t37 = (uint8_t)!t36;
                    if (!t37) goto lb28;
                    {
                        uint64_t t40 = gm_l1;
                        uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                        if (o42) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 219, 56);
                        uint64_t t43 = r41;
                        gm_l1 = t43;
                    }
lc28:;
                }
lb28:;
            }
            {
                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t45 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t46 = gm_l2;
                uint64_t t47 = (uintptr_t)t46;
                uint64_t t48 = gm_l1;
                uint64_t t49 = (uintptr_t)t48;
                if (t47 > t49 || t49 > (uintptr_t)t44) gm_trap_slice(t47, t49, t44, gm_src3d0v42kavhv7, 220, 15);
                gm_b16_8 t50;
                GM_ST(void *, (uint8_t *)&t50, (void *)(((uint8_t *)t45 + t47 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(t49 - t47));
                gm_b0_1 t51 = gm_f_for_body_ui39jfe(cx->env, cx->hctx, cx, t50);
                if (gm_unw.kind) {
                    goto gm_ll3;
                }
            }
lc2:;
        }
gm_ll3:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fields_ojgi3et_sq5308c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fields_ojgi3et F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* start */
    GM_STACK_CHECK(&F, 241u);
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
                for (;;) {
                    uint64_t t6 = gm_l1;
                    uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t8 = (t6 < t7);
                    if (!t8) goto lb5;
                    uint64_t t9 = gm_l1;
                    uint64_t t10 = (uintptr_t)t9;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_src3d0v42kavhv7, 216, 44);
                    uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t10 * 1));
                    uint8_t t13 = gm_f_is_space_uen38t5(t12);
                    if (!t13) goto lb5;
                    {
                        uint64_t t16 = gm_l1;
                        uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
                        if (o18) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 216, 52);
                        uint64_t t19 = r17;
                        gm_l1 = t19;
                    }
lc5:;
                }
lb5:;
            }
            {
                uint64_t t21 = gm_l1;
                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t23 = (t21 >= t22);
                if (!t23) goto ie20;
                {
                    gm_b0_1 t25;
                    uint8_t t26 = 0;
                    F.ret = t25;
                    goto gm_fret;
                }
                goto ix20;
ie20:;
ix20:;
            }
            {
                uint64_t t27 = gm_l1;
                gm_l2 = t27;
            }
            {
                for (;;) {
                    uint64_t t29 = gm_l1;
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t31 = (t29 < t30);
                    if (!t31) goto lb28;
                    uint64_t t32 = gm_l1;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 219, 48);
                    uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                    uint8_t t36 = gm_f_is_space_uen38t5(t35);
                    uint8_t t37 = (uint8_t)!t36;
                    if (!t37) goto lb28;
                    {
                        uint64_t t40 = gm_l1;
                        uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                        if (o42) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 219, 56);
                        uint64_t t43 = r41;
                        gm_l1 = t43;
                    }
lc28:;
                }
lb28:;
            }
            {
                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t45 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t46 = gm_l2;
                uint64_t t47 = (uintptr_t)t46;
                uint64_t t48 = gm_l1;
                uint64_t t49 = (uintptr_t)t48;
                if (t47 > t49 || t49 > (uintptr_t)t44) gm_trap_slice(t47, t49, t44, gm_src3d0v42kavhv7, 220, 15);
                gm_b16_8 t50;
                GM_ST(void *, (uint8_t *)&t50, (void *)(((uint8_t *)t45 + t47 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(t49 - t47));
                gm_b0_1 t51 = gm_f_for_body_1063u09(cx->env, cx->hctx, cx, t50);
            }
lc2:;
        }
lb2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_nab29co F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    uint64_t gm_l2; /* last */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t3 > t4);
        if (!t5) goto ie2;
        {
            gm_tf5srh4mp4b3i t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r11; bool o12 = __builtin_sub_overflow(t9, t10, &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 60, 13);
        uint64_t t13 = r11;
        gm_l2 = t13;
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t15 = gm_l3;
            uint64_t t16 = gm_l2;
            uint8_t t17 = (t15 <= t16);
            if (!t17) goto lb14;
            {
                gm_b16_8 t21 = gm_l0;
                gm_b16_8 t22 = gm_l1;
                uint64_t t23 = gm_l3;
                uint8_t t24 = gm_f_matches_at_jjpfr5u(t21, t22, t23);
                if (!t24) goto ie20;
                {
                    uint64_t t26 = gm_l3;
                    gm_tf5srh4mp4b3i t27;
                    GM_ST(uint8_t, (uint8_t *)&t27 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t27 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t27 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t27), t26);
                    ((uint8_t *)&t27)[8] = 1;
                    uint8_t t28 = 0;
                    F.ret = t27;
                    goto gm_fret;
                }
                goto ix20;
ie20:;
ix20:;
            }
            {
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 64, 9);
                uint64_t t32 = r30;
                gm_l3 = t32;
            }
lc14:;
        }
lb14:;
    }
    {
        gm_tf5srh4mp4b3i t33;
        memset(&t33, 0, sizeof t33);
        uint8_t t34 = 0;
        F.ret = t33;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_join_sfknljb F;
    gm_b16_8 gm_l0; /* parts */
    gm_b16_8 gm_l1; /* sep */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_b16_8 gm_l6; /* out */
    uint64_t gm_l7; /* at */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    GM_STACK_CHECK(&F, 806u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
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
                        uint64_t t17 = gm_l5;
                        uint8_t t18 = (t17 > ((uint64_t)0ULL));
                        if (!t18) goto ie16;
                        {
                            uint64_t t20 = gm_l2;
                            uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            uint64_t r22; bool o23 = __builtin_add_overflow(t20, t21, &r22);
                            if (o23) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 809, 20);
                            uint64_t t24 = r22;
                            gm_l2 = t24;
                        }
                        goto ix16;
ie16:;
ix16:;
                    }
                    {
                        uint64_t t25 = gm_l2;
                        uint64_t t26 = gm_l5;
                        uint64_t t27 = (uintptr_t)t26;
                        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_src3d0v42kavhv7, 810, 14);
                        uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t27 * 16) + 8);
                        uint64_t r30; bool o31 = __builtin_add_overflow(t25, t29, &r30);
                        if (o31) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 810, 9);
                        uint64_t t32 = r30;
                        gm_l2 = t32;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 808, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t34 = gm_l4;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l4 = t37;
                }
            }
lb4:;
        }
    }
    {
        uint64_t t38 = gm_l2;
        gm_tabb710d9mq2e t39;
        gm_f_make_2ip78bj((void *)&t39, (void *)NULL, cx, NULL, t38, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kh31phsjqu4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t40 = t39;
        if (((((uint8_t *)&t40))[0] != 0)) {
            gm_t9qkh0rsooc8d t41 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t40) + 8));
            gm_tabb710d9mq2e t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t42 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t42 + 16, 0);
            ((uint8_t *)&t42)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t42 + 8), t41);
            F.ret = t42;
            goto gm_fret;
        }
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
        gm_l6 = t43;
    }
    {
        gm_l7 = ((uint64_t)0ULL);
    }
    {
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l8 = t45;
        }
        {
            for (;;) {
                uint64_t t47 = gm_l9;
                uint64_t t48 = gm_l8;
                uint8_t t49 = (t47 < t48);
                if (!t49) goto lb46;
                {
                    uint64_t t52 = gm_l9;
                    uint64_t t53 = t52;
                    uint64_t t56 = GM_LD(uint64_t, ((uint8_t *)&t53));
                    gm_l10 = t56;
                    {
                        uint64_t t59 = gm_l10;
                        uint8_t t60 = (t59 > ((uint64_t)0ULL));
                        if (!t60) goto ie58;
                        {
                            uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            void * t63 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                            uint64_t t64 = gm_l7;
                            uint64_t t65 = (uintptr_t)t64;
                            uint64_t t66 = gm_l7;
                            uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            uint64_t r68; bool o69 = __builtin_add_overflow(t66, t67, &r68);
                            if (o69) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 816, 21);
                            uint64_t t70 = r68;
                            uint64_t t71 = (uintptr_t)t70;
                            if (t65 > t71 || t71 > (uintptr_t)t62) gm_trap_slice(t65, t71, t62, gm_src3d0v42kavhv7, 816, 13);
                            gm_b16_8 t72;
                            GM_ST(void *, (uint8_t *)&t72, (void *)(((uint8_t *)t63 + t65 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(t71 - t65));
                            gm_b16_8 t73 = gm_l1;
                            gm_f_copy_from_f7j6p05(t72, t73);
                        }
                        {
                            uint64_t t74 = gm_l7;
                            uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            uint64_t r76; bool o77 = __builtin_add_overflow(t74, t75, &r76);
                            if (o77) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 817, 13);
                            uint64_t t78 = r76;
                            gm_l7 = t78;
                        }
                        goto ix58;
ie58:;
ix58:;
                    }
                    {
                        uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        void * t80 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                        uint64_t t81 = gm_l7;
                        uint64_t t82 = (uintptr_t)t81;
                        uint64_t t83 = gm_l7;
                        uint64_t t84 = gm_l10;
                        uint64_t t85 = (uintptr_t)t84;
                        uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t85 >= (uintptr_t)(t86)) gm_trap_index(t85, t86, gm_src3d0v42kavhv7, 819, 22);
                        uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t85 * 16) + 8);
                        uint64_t r88; bool o89 = __builtin_add_overflow(t83, t87, &r88);
                        if (o89) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 819, 17);
                        uint64_t t90 = r88;
                        uint64_t t91 = (uintptr_t)t90;
                        if (t82 > t91 || t91 > (uintptr_t)t79) gm_trap_slice(t82, t91, t79, gm_src3d0v42kavhv7, 819, 9);
                        gm_b16_8 t92;
                        GM_ST(void *, (uint8_t *)&t92, (void *)(((uint8_t *)t80 + t82 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(t91 - t82));
                        uint64_t t93 = gm_l10;
                        uint64_t t94 = (uintptr_t)t93;
                        uint64_t t95 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t94 >= (uintptr_t)(t95)) gm_trap_index(t94, t95, gm_src3d0v42kavhv7, 819, 46);
                        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t94 * 16));
                        gm_f_copy_from_f7j6p05(t92, t96);
                    }
                    {
                        uint64_t t97 = gm_l7;
                        uint64_t t98 = gm_l10;
                        uint64_t t99 = (uintptr_t)t98;
                        uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t99 >= (uintptr_t)(t100)) gm_trap_index(t99, t100, gm_src3d0v42kavhv7, 820, 15);
                        uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t99 * 16) + 8);
                        uint64_t r102; bool o103 = __builtin_add_overflow(t97, t101, &r102);
                        if (o103) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 820, 9);
                        uint64_t t104 = r102;
                        gm_l7 = t104;
                    }
                    goto me54;
mn55:;
                    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 814, 5);
me54:;
                }
lc46:;
                {
                    uint64_t t106 = gm_l9;
                    uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                    uint64_t t109 = r107;
                    gm_l9 = t109;
                }
            }
lb46:;
        }
    }
    {
        gm_b16_8 t110 = gm_l6;
        gm_tabb710d9mq2e t111;
        GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
        ((uint8_t *)&t111)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t111 + 8), t110);
        gm_tabb710d9mq2e t112 = t111;
        uint8_t t113 = ((((uint8_t *)&t112))[0] != 0);
        F.ret = t111;
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
__attribute__((aligned(4))) void gm_f_left_shift_77i68r4(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_left_shift_77i68r4 F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* k */
    uint64_t gm_l3; /* end */
    uint64_t gm_l4; /* p */
    uint64_t gm_l5; /* n */
    uint64_t gm_l6; /* r */
    uint64_t gm_l7; /* quo */
    uint64_t gm_l8; /* quo */
    uint64_t gm_l9; /* start */
    uint64_t gm_l10; /* keep */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 867u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
    }
    {
        gm_l3 = ((uint64_t)832ULL);
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 800));
        gm_l6 = t3;
    }
    {
        for (;;) {
            uint64_t t5 = gm_l6;
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            if (!t6) goto lb4;
            {
                uint64_t t9 = gm_l6;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 480, 9);
                uint64_t t12 = r10;
                gm_l6 = t12;
            }
            {
                uint64_t t13 = gm_l5;
                uint64_t t14 = gm_l6;
                uint64_t t15 = (uintptr_t)t14;
                void * t16 = gm_l0;
                if (t15 >= (uintptr_t)(800)) gm_trap_index(t15, 800, gm_src3d0v42kavhv7, 481, 18);
                uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 0) + t15 * 1));
                uint64_t t18 = (uint64_t)t17;
                uint32_t t19 = gm_l1;
                unsigned __int128 n20 = (unsigned __int128)t19;
                if (n20 >= 64) gm_trap_shift((uint64_t)n20, "u64", gm_src3d0v42kavhv7, 481, 14);
                uint64_t r21 = (uint64_t)((uint64_t)t18 << (int)n20);
                bool o22 = (uint64_t)(r21 >> (int)n20) != t18;
                if (o22) gm_trap("integer overflow in `<<`", gm_src3d0v42kavhv7, 481, 14);
                uint64_t t23 = r21;
                uint64_t r24; bool o25 = __builtin_add_overflow(t13, t23, &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 481, 9);
                uint64_t t26 = r24;
                gm_l5 = t26;
            }
            {
                uint64_t t27 = gm_l5;
                if (((uint64_t)10ULL) == 0) gm_trap("division by zero", gm_src3d0v42kavhv7, 482, 16);
                uint64_t t28 = (uint64_t)(t27 / ((uint64_t)10ULL));
                gm_l7 = t28;
            }
            {
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 483, 17);
                uint64_t t32 = r30;
                uint64_t t33 = gm_l4;
                uint64_t r34; bool o35 = __builtin_sub_overflow(t32, t33, &r34);
                if (o35) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 483, 17);
                uint64_t t36 = r34;
                uint64_t t37 = (uintptr_t)t36;
                if (t37 >= (uintptr_t)(832)) gm_trap_index(t37, 832, gm_src3d0v42kavhv7, 483, 9);
                uint64_t t38 = gm_l5;
                uint64_t t39 = gm_l7;
                uint64_t r40; bool o41 = __builtin_mul_overflow(((uint64_t)10ULL), t39, &r40);
                if (o41) gm_trap("integer overflow in `*`", gm_src3d0v42kavhv7, 483, 37);
                uint64_t t42 = r40;
                uint64_t r43; bool o44 = __builtin_sub_overflow(t38, t42, &r43);
                if (o44) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 483, 33);
                uint64_t t45 = r43;
                uint8_t t46 = (uint8_t)t45;
                GM_ST(uint8_t, (((uint8_t *)&F.l2) + t37 * 1), t46);
            }
            {
                uint64_t t47 = gm_l4;
                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                if (o49) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 484, 9);
                uint64_t t50 = r48;
                gm_l4 = t50;
            }
            {
                uint64_t t51 = gm_l7;
                gm_l5 = t51;
            }
lc4:;
        }
lb4:;
    }
    {
        for (;;) {
            uint64_t t53 = gm_l5;
            uint8_t t54 = (t53 > ((uint64_t)0ULL));
            if (!t54) goto lb52;
            {
                uint64_t t57 = gm_l5;
                if (((uint64_t)10ULL) == 0) gm_trap("division by zero", gm_src3d0v42kavhv7, 488, 16);
                uint64_t t58 = (uint64_t)(t57 / ((uint64_t)10ULL));
                gm_l8 = t58;
            }
            {
                uint64_t t59 = gm_l3;
                uint64_t r60; bool o61 = __builtin_sub_overflow(t59, ((uint64_t)1ULL), &r60);
                if (o61) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 489, 17);
                uint64_t t62 = r60;
                uint64_t t63 = gm_l4;
                uint64_t r64; bool o65 = __builtin_sub_overflow(t62, t63, &r64);
                if (o65) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 489, 17);
                uint64_t t66 = r64;
                uint64_t t67 = (uintptr_t)t66;
                if (t67 >= (uintptr_t)(832)) gm_trap_index(t67, 832, gm_src3d0v42kavhv7, 489, 9);
                uint64_t t68 = gm_l5;
                uint64_t t69 = gm_l8;
                uint64_t r70; bool o71 = __builtin_mul_overflow(((uint64_t)10ULL), t69, &r70);
                if (o71) gm_trap("integer overflow in `*`", gm_src3d0v42kavhv7, 489, 37);
                uint64_t t72 = r70;
                uint64_t r73; bool o74 = __builtin_sub_overflow(t68, t72, &r73);
                if (o74) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 489, 33);
                uint64_t t75 = r73;
                uint8_t t76 = (uint8_t)t75;
                GM_ST(uint8_t, (((uint8_t *)&F.l2) + t67 * 1), t76);
            }
            {
                uint64_t t77 = gm_l4;
                uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                if (o79) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 490, 9);
                uint64_t t80 = r78;
                gm_l4 = t80;
            }
            {
                uint64_t t81 = gm_l8;
                gm_l5 = t81;
            }
lc52:;
        }
lb52:;
    }
    {
        uint64_t t82 = gm_l3;
        uint64_t t83 = gm_l4;
        uint64_t r84; bool o85 = __builtin_sub_overflow(t82, t83, &r84);
        if (o85) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 493, 14);
        uint64_t t86 = r84;
        gm_l9 = t86;
    }
    {
        uint64_t t87;
        uint64_t t89 = gm_l4;
        uint8_t t90 = (t89 > ((uint64_t)800ULL));
        if (!t90) goto ie88;
        uint64_t t92 = ((uint64_t)800ULL);
        t87 = t92;
        goto ix88;
ie88:;
        uint64_t t94 = gm_l4;
        uint64_t t95 = t94;
        t87 = t95;
ix88:;
        gm_l10 = t87;
    }
    {
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            uint64_t t97 = gm_l10;
            gm_l11 = t97;
        }
        {
            for (;;) {
                uint64_t t99 = gm_l12;
                uint64_t t100 = gm_l11;
                uint8_t t101 = (t99 < t100);
                if (!t101) goto lb98;
                {
                    uint64_t t104 = gm_l12;
                    uint64_t t105 = t104;
                    uint64_t t108 = GM_LD(uint64_t, ((uint8_t *)&t105));
                    gm_l13 = t108;
                    {
                        uint64_t t110 = gm_l13;
                        uint64_t t111 = (uintptr_t)t110;
                        void * t112 = gm_l0;
                        if (t111 >= (uintptr_t)(800)) gm_trap_index(t111, 800, gm_src3d0v42kavhv7, 495, 24);
                        uint64_t t113 = gm_l9;
                        uint64_t t114 = gm_l13;
                        uint64_t r115; bool o116 = __builtin_add_overflow(t113, t114, &r115);
                        if (o116) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 495, 41);
                        uint64_t t117 = r115;
                        uint64_t t118 = (uintptr_t)t117;
                        if (t118 >= (uintptr_t)(832)) gm_trap_index(t118, 832, gm_src3d0v42kavhv7, 495, 33);
                        uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + t118 * 1));
                        GM_ST(uint8_t, ((((uint8_t *)t112) + 0) + t111 * 1), t119);
                    }
                    goto me106;
mn107:;
                    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 495, 5);
me106:;
                }
lc98:;
                {
                    uint64_t t121 = gm_l12;
                    uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                    uint64_t t124 = r122;
                    gm_l12 = t124;
                }
            }
lb98:;
        }
    }
    {
        {
            uint64_t t126 = gm_l10;
            gm_l15 = t126;
        }
        {
            uint64_t t127 = gm_l4;
            gm_l14 = t127;
        }
        {
            for (;;) {
                uint64_t t129 = gm_l15;
                uint64_t t130 = gm_l14;
                uint8_t t131 = (t129 < t130);
                if (!t131) goto lb128;
                {
                    uint64_t t134 = gm_l15;
                    uint64_t t135 = t134;
                    uint64_t t138 = GM_LD(uint64_t, ((uint8_t *)&t135));
                    gm_l16 = t138;
                    {
                        uint64_t t141 = gm_l9;
                        uint64_t t142 = gm_l16;
                        uint64_t r143; bool o144 = __builtin_add_overflow(t141, t142, &r143);
                        if (o144) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 497, 20);
                        uint64_t t145 = r143;
                        uint64_t t146 = (uintptr_t)t145;
                        if (t146 >= (uintptr_t)(832)) gm_trap_index(t146, 832, gm_src3d0v42kavhv7, 497, 12);
                        uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + t146 * 1));
                        uint8_t t148 = (!(t147 == ((uint8_t)0ULL)));
                        if (!t148) goto ie140;
                        {
                            void * t150 = gm_l0;
                            GM_ST(uint8_t, (((uint8_t *)t150) + 816), ((uint8_t)1ULL));
                        }
                        goto ix140;
ie140:;
ix140:;
                    }
                    goto me136;
mn137:;
                    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 496, 5);
me136:;
                }
lc128:;
                {
                    uint64_t t152 = gm_l15;
                    uint64_t r153; bool o154 = __builtin_add_overflow(t152, ((uint64_t)1ULL), &r153);
                    uint64_t t155 = r153;
                    gm_l15 = t155;
                }
            }
lb128:;
        }
    }
    {
        void * t156 = gm_l0;
        void * t157 = gm_l0;
        int64_t t158 = GM_LD(int64_t, (((uint8_t *)t157) + 808));
        uint64_t t159 = gm_l4;
        int64_t t160 = (int64_t)t159;
        void * t161 = gm_l0;
        uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)t161) + 800));
        int64_t t163 = (int64_t)t162;
        int64_t r164; bool o165 = __builtin_sub_overflow(t160, t163, &r164);
        if (o165) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 499, 13);
        int64_t t166 = r164;
        int64_t r167; bool o168 = __builtin_add_overflow(t158, t166, &r167);
        if (o168) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 499, 5);
        int64_t t169 = r167;
        GM_ST(int64_t, (((uint8_t *)t156) + 808), t169);
    }
    {
        void * t170 = gm_l0;
        uint64_t t171 = gm_l10;
        GM_ST(uint64_t, (((uint8_t *)t170) + 800), t171);
    }
    {
        void * t172 = gm_l0;
        gm_f_trim_decimal_56e5p3i(t172);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_node *pn49 = gm_find(cx, 993381376);
                if (!pn49) gm_trap("no handler installed for effect `Yield`", gm_src3d0v42kavhv7, 206, 9);
                gm_b0_1 t50;
                t50 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn49->clauses[0])(pn49->env, pn49->hctx, pn49, t48);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
            }
            {
                uint64_t t51 = gm_l2;
                uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                if (o53) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t54 = r52;
                gm_l1 = t54;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s1f7rak(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_n9765ef(cx->env, cx->hctx, cx, t48);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s1hv6k0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_j11vulv(cx->env, cx->hctx, cx, t48);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s30klli(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_3400t3p(cx->env, cx->hctx, cx, t48);
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s84ppfm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_gp0er0a(cx->env, cx->hctx, cx, t48);
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s9kjq5m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_tv3u4f0(cx->env, cx->hctx, cx, t48);
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sa157kc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_fe8qj55(cx->env, cx->hctx, cx, t48);
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_samvo8i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_ukps8ob(cx->env, cx->hctx, cx, t48);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_scfak7n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_hrv26t6(cx->env, cx->hctx, cx, t48);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sersjf1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_ig27ig2(cx->env, cx->hctx, cx, t48);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sfohpq4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_keqa9hq(cx->env, cx->hctx, cx, t48);
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sld76pa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_8edr2np(cx->env, cx->hctx, cx, t48);
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_snhh6ss(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_jvjn1fs(cx->env, cx->hctx, cx, t48);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_snvhnj5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_vn00ddp(cx->env, cx->hctx, cx, t48);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sp8kip0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_ui0bpsf(cx->env, cx->hctx, cx, t48);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sqdm19s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_vetgq3s(cx->env, cx->hctx, cx, t48);
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sssrp43(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_tdd1e60(cx->env, cx->hctx, cx, t48);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_svejqbu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lines_9uns0k2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line_end */
    GM_STACK_CHECK(&F, 279u);
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
            {
                uint64_t t8 = gm_l1;
                gm_l2 = t8;
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src3d0v42kavhv7, 203, 31);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
                    uint8_t t17 = (!(t16 == ((uint8_t)10ULL)));
                    if (!t17) goto lb9;
                    {
                        uint64_t t20 = gm_l2;
                        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 203, 52);
                        uint64_t t23 = r21;
                        gm_l2 = t23;
                    }
lc9:;
                }
lb9:;
            }
            {
                uint64_t t24 = gm_l2;
                gm_l3 = t24;
            }
            {
                uint64_t t26 = gm_l3;
                uint64_t t27 = gm_l1;
                uint8_t t28 = (t26 > t27);
                if (!t28) goto ie25;
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 35);
                uint64_t t32 = r30;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src3d0v42kavhv7, 205, 33);
                uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                uint8_t t36 = ((t35 == ((uint8_t)13ULL)));
                if (!t36) goto ie25;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
                    if (o40) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 205, 59);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t44 = gm_l1;
                uint64_t t45 = (uintptr_t)t44;
                uint64_t t46 = gm_l3;
                uint64_t t47 = (uintptr_t)t46;
                if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_src3d0v42kavhv7, 206, 15);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                gm_b0_1 t49 = gm_f_for_body_69vpqmq(cx->env, cx->hctx, cx, t48);
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
            }
            {
                uint64_t t50 = gm_l2;
                uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                if (o52) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 207, 17);
                uint64_t t53 = r51;
                gm_l1 = t53;
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
inline __attribute__((aligned(4))) uint8_t gm_f_matches_at_jjpfr5u(gm_b16_8 a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_matches_at_jjpfr5u F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    uint64_t gm_l2; /* at */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 200u);
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
                        uint64_t t17 = gm_l2;
                        uint64_t t18 = gm_l5;
                        uint64_t r19; bool o20 = __builtin_add_overflow(t17, t18, &r19);
                        if (o20) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 51, 14);
                        uint64_t t21 = r19;
                        uint64_t t22 = (uintptr_t)t21;
                        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_src3d0v42kavhv7, 51, 12);
                        uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t22 * 1));
                        uint64_t t25 = gm_l5;
                        uint64_t t26 = (uintptr_t)t25;
                        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_src3d0v42kavhv7, 51, 29);
                        uint8_t t28 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t26 * 1));
                        uint8_t t29 = (!(t24 == t28));
                        if (!t29) goto ie16;
                        {
                            uint8_t t31 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix16;
ie16:;
ix16:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 50, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t33 = gm_l4;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l4 = t36;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t37 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_replace_alloc_t7hndlq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_replace_alloc_t7hndlq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* from */
    gm_b16_8 gm_l2; /* to */
    uint64_t gm_l3; /* n */
    gm_b16_8 gm_l4; /* out */
    uint64_t gm_l5; /* at */
    gm_b16_8 gm_l6; /* rest */
    uint64_t gm_l7; /* i */
    GM_STACK_CHECK(&F, 922u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        uint64_t t4 = gm_f_count_9fkti0l(t2, t3);
        gm_l3 = t4;
    }
    {
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t6 = gm_l3;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r8; bool o9 = __builtin_mul_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `*`", gm_src3d0v42kavhv7, 274, 33);
        uint64_t t10 = r8;
        uint64_t r11; bool o12 = __builtin_sub_overflow(t5, t10, &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 274, 25);
        uint64_t t13 = r11;
        uint64_t t14 = gm_l3;
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint64_t r16; bool o17 = __builtin_mul_overflow(t14, t15, &r16);
        if (o17) gm_trap("integer overflow in `*`", gm_src3d0v42kavhv7, 274, 48);
        uint64_t t18 = r16;
        uint64_t r19; bool o20 = __builtin_add_overflow(t13, t18, &r19);
        if (o20) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 274, 25);
        uint64_t t21 = r19;
        gm_tabb710d9mq2e t22;
        gm_f_make_2ip78bj((void *)&t22, (void *)NULL, cx, NULL, t21, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k6oa34t4v6n)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) {
            gm_t9qkh0rsooc8d t24 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t23) + 8));
            gm_tabb710d9mq2e t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t25 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t25 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t25 + 16, 0);
            ((uint8_t *)&t25)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 8), t24);
            F.ret = t25;
            goto gm_fret;
        }
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
        gm_l4 = t26;
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        gm_b16_8 t27 = gm_l0;
        gm_l6 = t27;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb28;
            {
                gm_b16_8 t31 = gm_l6;
                gm_b16_8 t32 = gm_l1;
                gm_tf5srh4mp4b3i t33 = gm_f_find_nab29co(t31, t32);
                gm_tf5srh4mp4b3i t34 = t33;
                uint64_t t35;
                if (((((uint8_t *)&t34))[8] != 0)) {
                    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&t34) + 0));
                    t35 = t36;
                } else {
                    goto lb28;
                }
                gm_l7 = t35;
            }
            {
                uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                void * t38 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                uint64_t t39 = gm_l5;
                uint64_t t40 = (uintptr_t)t39;
                uint64_t t41 = gm_l5;
                uint64_t t42 = gm_l7;
                uint64_t r43; bool o44 = __builtin_add_overflow(t41, t42, &r43);
                if (o44) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 279, 17);
                uint64_t t45 = r43;
                uint64_t t46 = (uintptr_t)t45;
                if (t40 > t46 || t46 > (uintptr_t)t37) gm_trap_slice(t40, t46, t37, gm_src3d0v42kavhv7, 279, 9);
                gm_b16_8 t47;
                GM_ST(void *, (uint8_t *)&t47, (void *)(((uint8_t *)t38 + t40 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(t46 - t40));
                uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                void * t49 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                uint64_t t50 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t51 = gm_l7;
                uint64_t t52 = (uintptr_t)t51;
                if (t50 > t52 || t52 > (uintptr_t)t48) gm_trap_slice(t50, t52, t48, gm_src3d0v42kavhv7, 279, 35);
                gm_b16_8 t53;
                GM_ST(void *, (uint8_t *)&t53, (void *)(((uint8_t *)t49 + t50 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(t52 - t50));
                gm_f_copy_from_f7j6p05(t47, t53);
            }
            {
                uint64_t t54 = gm_l5;
                uint64_t t55 = gm_l7;
                uint64_t r56; bool o57 = __builtin_add_overflow(t54, t55, &r56);
                if (o57) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 280, 9);
                uint64_t t58 = r56;
                gm_l5 = t58;
            }
            {
                uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                void * t60 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                uint64_t t61 = gm_l5;
                uint64_t t62 = (uintptr_t)t61;
                uint64_t t63 = gm_l5;
                uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint64_t r65; bool o66 = __builtin_add_overflow(t63, t64, &r65);
                if (o66) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 281, 17);
                uint64_t t67 = r65;
                uint64_t t68 = (uintptr_t)t67;
                if (t62 > t68 || t68 > (uintptr_t)t59) gm_trap_slice(t62, t68, t59, gm_src3d0v42kavhv7, 281, 9);
                gm_b16_8 t69;
                GM_ST(void *, (uint8_t *)&t69, (void *)(((uint8_t *)t60 + t62 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(t68 - t62));
                gm_b16_8 t70 = gm_l2;
                gm_f_copy_from_f7j6p05(t69, t70);
            }
            {
                uint64_t t71 = gm_l5;
                uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint64_t r73; bool o74 = __builtin_add_overflow(t71, t72, &r73);
                if (o74) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 282, 9);
                uint64_t t75 = r73;
                gm_l5 = t75;
            }
            {
                uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                void * t77 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                uint64_t t78 = gm_l7;
                uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint64_t r80; bool o81 = __builtin_add_overflow(t78, t79, &r80);
                if (o81) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 283, 21);
                uint64_t t82 = r80;
                uint64_t t83 = (uintptr_t)t82;
                uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint64_t t85 = (uintptr_t)t84;
                if (t83 > t85 || t85 > (uintptr_t)t76) gm_trap_slice(t83, t85, t76, gm_src3d0v42kavhv7, 283, 16);
                gm_b16_8 t86;
                GM_ST(void *, (uint8_t *)&t86, (void *)(((uint8_t *)t77 + t83 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(t85 - t83));
                gm_l6 = t86;
            }
lc28:;
        }
lb28:;
    }
    {
        uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        void * t88 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
        uint64_t t89 = gm_l5;
        uint64_t t90 = (uintptr_t)t89;
        uint64_t t91 = gm_l5;
        uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
        uint64_t r93; bool o94 = __builtin_add_overflow(t91, t92, &r93);
        if (o94) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 285, 13);
        uint64_t t95 = r93;
        uint64_t t96 = (uintptr_t)t95;
        if (t90 > t96 || t96 > (uintptr_t)t87) gm_trap_slice(t90, t96, t87, gm_src3d0v42kavhv7, 285, 5);
        gm_b16_8 t97;
        GM_ST(void *, (uint8_t *)&t97, (void *)(((uint8_t *)t88 + t90 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(t96 - t90));
        gm_b16_8 t98 = gm_l6;
        gm_f_copy_from_f7j6p05(t97, t98);
    }
    {
        gm_b16_8 t99 = gm_l4;
        gm_tabb710d9mq2e t100;
        GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
        ((uint8_t *)&t100)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t100 + 8), t99);
        gm_tabb710d9mq2e t101 = t100;
        uint8_t t102 = ((((uint8_t *)&t101))[0] != 0);
        F.ret = t100;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_replace_lf6hcmn(void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_replace_lf6hcmn F;
    void * gm_l0; /* out */
    gm_b16_8 gm_l1; /* s */
    gm_b16_8 gm_l2; /* from */
    gm_b16_8 gm_l3; /* to */
    gm_b16_8 gm_l4; /* rest */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 438u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
            if (!t5) goto ie3;
            {
                gm_b16_8 t7;
                GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s6hiq18oa7ku0));
                GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(31));
                gm_b16_8 t8 = t7;
                gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t8)), GM_LD(uintptr_t, ((uint8_t *)&t8) + 8), gm_src3d0v42kavhv7, 259, 24);
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            gm_b16_8 t9 = gm_l1;
            gm_l4 = t9;
        }
        {
            for (;;) {
                if (!((uint8_t)1)) goto lb10;
                {
                    gm_b16_8 t13 = gm_l4;
                    gm_b16_8 t14 = gm_l2;
                    gm_tf5srh4mp4b3i t15 = gm_f_find_nab29co(t13, t14);
                    gm_tf5srh4mp4b3i t16 = t15;
                    uint64_t t17;
                    if (((((uint8_t *)&t16))[8] != 0)) {
                        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                        t17 = t18;
                    } else {
                        goto lb10;
                    }
                    gm_l5 = t17;
                }
                {
                    void * t19 = gm_l0;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    void * t21 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                    uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t23 = gm_l5;
                    uint64_t t24 = (uintptr_t)t23;
                    if (t22 > t24 || t24 > (uintptr_t)t20) gm_trap_slice(t22, t24, t20, gm_src3d0v42kavhv7, 263, 24);
                    gm_b16_8 t25;
                    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t21 + t22 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24 - t22));
                    gm_tftb1odc739t5 t26 = gm_f_extend_0cs85gs(t19, t25);
                    gm_tftb1odc739t5 t27 = t26;
                    if (((((uint8_t *)&t27))[0] != 0)) {
                        gm_t9qkh0rsooc8d t28 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t27) + 1));
                        gm_tftb1odc739t5 t29;
                        ((uint8_t *)&t29)[0] = 1;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 1), t28);
                        F.ret = t29;
                        goto gm_fret;
                    }
                    gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                }
                {
                    void * t31 = gm_l0;
                    gm_b16_8 t32 = gm_l3;
                    gm_tftb1odc739t5 t33 = gm_f_extend_0cs85gs(t31, t32);
                    gm_tftb1odc739t5 t34 = t33;
                    if (((((uint8_t *)&t34))[0] != 0)) {
                        gm_t9qkh0rsooc8d t35 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t34) + 1));
                        gm_tftb1odc739t5 t36;
                        ((uint8_t *)&t36)[0] = 1;
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 1), t35);
                        F.ret = t36;
                        goto gm_fret;
                    }
                    gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
                }
                {
                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    void * t39 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                    uint64_t t40 = gm_l5;
                    uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
                    if (o43) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 265, 21);
                    uint64_t t44 = r42;
                    uint64_t t45 = (uintptr_t)t44;
                    uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint64_t t47 = (uintptr_t)t46;
                    if (t45 > t47 || t47 > (uintptr_t)t38) gm_trap_slice(t45, t47, t38, gm_src3d0v42kavhv7, 265, 16);
                    gm_b16_8 t48;
                    GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t39 + t45 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
                    gm_l4 = t48;
                }
lc10:;
            }
lb10:;
        }
        {
            void * t49 = gm_l0;
            gm_b16_8 t50 = gm_l4;
            gm_tftb1odc739t5 t51 = gm_f_extend_0cs85gs(t49, t50);
            gm_tftb1odc739t5 t52 = t51;
            if (((((uint8_t *)&t52))[0] != 0)) {
                gm_t9qkh0rsooc8d t53 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t52) + 1));
                gm_tftb1odc739t5 t54;
                ((uint8_t *)&t54)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 1), t53);
                F.ret = t54;
                goto gm_fret;
            }
            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
        }
    }
    gm_b0_1 t56;
    gm_tftb1odc739t5 t57;
    GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
    ((uint8_t *)&t57)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t57 + 1), t56);
    gm_tftb1odc739t5 t58 = t57;
    F.ret = t58;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rfind_ai5s2js(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rfind_ai5s2js F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 246u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t3 > t4);
        if (!t5) goto ie2;
        {
            gm_tf5srh4mp4b3i t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r11; bool o12 = __builtin_sub_overflow(t9, t10, &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 73, 10);
        uint64_t t13 = r11;
        uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)1ULL), &r14);
        if (o15) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 73, 10);
        uint64_t t16 = r14;
        gm_l2 = t16;
    }
    {
        for (;;) {
            uint64_t t18 = gm_l2;
            uint8_t t19 = (t18 > ((uint64_t)0ULL));
            if (!t19) goto lb17;
            {
                uint64_t t22 = gm_l2;
                uint64_t r23; bool o24 = __builtin_sub_overflow(t22, ((uint64_t)1ULL), &r23);
                if (o24) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 75, 9);
                uint64_t t25 = r23;
                gm_l2 = t25;
            }
            {
                gm_b16_8 t27 = gm_l0;
                gm_b16_8 t28 = gm_l1;
                uint64_t t29 = gm_l2;
                uint8_t t30 = gm_f_matches_at_jjpfr5u(t27, t28, t29);
                if (!t30) goto ie26;
                {
                    uint64_t t32 = gm_l2;
                    gm_tf5srh4mp4b3i t33;
                    GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t33), t32);
                    ((uint8_t *)&t33)[8] = 1;
                    uint8_t t34 = 0;
                    F.ret = t33;
                    goto gm_fret;
                }
                goto ix26;
ie26:;
ix26:;
            }
lc17:;
        }
lb17:;
    }
    {
        gm_tf5srh4mp4b3i t35;
        memset(&t35, 0, sizeof t35);
        uint8_t t36 = 0;
        F.ret = t35;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_right_shift_22mtui8(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_right_shift_22mtui8 F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* k */
    uint64_t gm_l2; /* r */
    uint64_t gm_l3; /* w */
    uint64_t gm_l4; /* n */
    uint64_t gm_l5; /* mask */
    uint64_t gm_l6; /* dig */
    uint64_t gm_l7; /* dig */
    GM_STACK_CHECK_SHALLOW(&F, 685u);
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
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l4;
            uint32_t t4 = gm_l1;
            unsigned __int128 n5 = (unsigned __int128)t4;
            if (n5 >= 64) gm_trap_shift((uint64_t)n5, "u64", gm_src3d0v42kavhv7, 431, 12);
            uint64_t t6 = (uint64_t)(t3 >> (int)n5);
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            if (!t7) goto lb2;
            {
                uint64_t t11 = gm_l2;
                void * t12 = gm_l0;
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 800));
                uint8_t t14 = (t11 >= t13);
                if (!t14) goto ie10;
                {
                    uint64_t t17 = gm_l4;
                    uint8_t t18 = ((t17 == ((uint64_t)0ULL)));
                    if (!t18) goto ie16;
                    {
                        void * t20 = gm_l0;
                        GM_ST(uint64_t, (((uint8_t *)t20) + 800), ((uint64_t)0ULL));
                    }
                    {
                        gm_b0_1 t21;
                        uint8_t t22 = 0;
                        F.ret = t21;
                        goto gm_fret;
                    }
                    goto ix16;
ie16:;
ix16:;
                }
                {
                    for (;;) {
                        uint64_t t24 = gm_l4;
                        uint32_t t25 = gm_l1;
                        unsigned __int128 n26 = (unsigned __int128)t25;
                        if (n26 >= 64) gm_trap_shift((uint64_t)n26, "u64", gm_src3d0v42kavhv7, 437, 20);
                        uint64_t t27 = (uint64_t)(t24 >> (int)n26);
                        uint8_t t28 = ((t27 == ((uint64_t)0ULL)));
                        if (!t28) goto lb23;
                        {
                            uint64_t t31 = gm_l4;
                            uint64_t r32; bool o33 = __builtin_mul_overflow(t31, ((uint64_t)10ULL), &r32);
                            if (o33) gm_trap("integer overflow in `*`", gm_src3d0v42kavhv7, 438, 17);
                            uint64_t t34 = r32;
                            gm_l4 = t34;
                        }
                        {
                            uint64_t t35 = gm_l2;
                            uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                            if (o37) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 439, 17);
                            uint64_t t38 = r36;
                            gm_l2 = t38;
                        }
lc23:;
                    }
lb23:;
                }
                {
                    goto lb2;
                }
                goto ix10;
ie10:;
ix10:;
            }
            {
                uint64_t t39 = gm_l4;
                uint64_t r40; bool o41 = __builtin_mul_overflow(t39, ((uint64_t)10ULL), &r40);
                if (o41) gm_trap("integer overflow in `*`", gm_src3d0v42kavhv7, 443, 13);
                uint64_t t42 = r40;
                uint64_t t43 = gm_l2;
                uint64_t t44 = (uintptr_t)t43;
                void * t45 = gm_l0;
                if (t44 >= (uintptr_t)(800)) gm_trap_index(t44, 800, gm_src3d0v42kavhv7, 443, 26);
                uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t45) + 0) + t44 * 1));
                uint64_t t47 = (uint64_t)t46;
                uint64_t r48; bool o49 = __builtin_add_overflow(t42, t47, &r48);
                if (o49) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 443, 13);
                uint64_t t50 = r48;
                gm_l4 = t50;
            }
            {
                uint64_t t51 = gm_l2;
                uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                if (o53) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 444, 9);
                uint64_t t54 = r52;
                gm_l2 = t54;
            }
lc2:;
        }
lb2:;
    }
    {
        void * t55 = gm_l0;
        void * t56 = gm_l0;
        int64_t t57 = GM_LD(int64_t, (((uint8_t *)t56) + 808));
        uint64_t t58 = gm_l2;
        int64_t t59 = (int64_t)t58;
        int64_t r60; bool o61 = __builtin_sub_overflow(t59, ((int64_t)1LL), &r60);
        if (o61) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 446, 13);
        int64_t t62 = r60;
        int64_t r63; bool o64 = __builtin_sub_overflow(t57, t62, &r63);
        if (o64) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 446, 5);
        int64_t t65 = r63;
        GM_ST(int64_t, (((uint8_t *)t55) + 808), t65);
    }
    {
        uint32_t t66 = gm_l1;
        unsigned __int128 n67 = (unsigned __int128)t66;
        if (n67 >= 64) gm_trap_shift((uint64_t)n67, "u64", gm_src3d0v42kavhv7, 447, 18);
        uint64_t r68 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n67);
        bool o69 = (uint64_t)(r68 >> (int)n67) != ((uint64_t)1ULL);
        if (o69) gm_trap("integer overflow in `<<`", gm_src3d0v42kavhv7, 447, 18);
        uint64_t t70 = r68;
        uint64_t r71; bool o72 = __builtin_sub_overflow(t70, ((uint64_t)1ULL), &r71);
        if (o72) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 447, 17);
        uint64_t t73 = r71;
        gm_l5 = t73;
    }
    {
        for (;;) {
            uint64_t t75 = gm_l2;
            void * t76 = gm_l0;
            uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)t76) + 800));
            uint8_t t78 = (t75 < t77);
            if (!t78) goto lb74;
            {
                uint64_t t81 = gm_l4;
                uint32_t t82 = gm_l1;
                unsigned __int128 n83 = (unsigned __int128)t82;
                if (n83 >= 64) gm_trap_shift((uint64_t)n83, "u64", gm_src3d0v42kavhv7, 449, 16);
                uint64_t t84 = (uint64_t)(t81 >> (int)n83);
                gm_l6 = t84;
            }
            {
                uint64_t t85 = gm_l4;
                uint64_t t86 = gm_l5;
                uint64_t t87 = (uint64_t)(t85 & t86);
                gm_l4 = t87;
            }
            {
                uint64_t t88 = gm_l3;
                uint64_t t89 = (uintptr_t)t88;
                void * t90 = gm_l0;
                if (t89 >= (uintptr_t)(800)) gm_trap_index(t89, 800, gm_src3d0v42kavhv7, 451, 9);
                uint64_t t91 = gm_l6;
                uint8_t t92 = (uint8_t)t91;
                GM_ST(uint8_t, ((((uint8_t *)t90) + 0) + t89 * 1), t92);
            }
            {
                uint64_t t93 = gm_l3;
                uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                if (o95) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 452, 9);
                uint64_t t96 = r94;
                gm_l3 = t96;
            }
            {
                uint64_t t97 = gm_l4;
                uint64_t r98; bool o99 = __builtin_mul_overflow(t97, ((uint64_t)10ULL), &r98);
                if (o99) gm_trap("integer overflow in `*`", gm_src3d0v42kavhv7, 453, 13);
                uint64_t t100 = r98;
                uint64_t t101 = gm_l2;
                uint64_t t102 = (uintptr_t)t101;
                void * t103 = gm_l0;
                if (t102 >= (uintptr_t)(800)) gm_trap_index(t102, 800, gm_src3d0v42kavhv7, 453, 26);
                uint8_t t104 = GM_LD(uint8_t, ((((uint8_t *)t103) + 0) + t102 * 1));
                uint64_t t105 = (uint64_t)t104;
                uint64_t r106; bool o107 = __builtin_add_overflow(t100, t105, &r106);
                if (o107) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 453, 13);
                uint64_t t108 = r106;
                gm_l4 = t108;
            }
            {
                uint64_t t109 = gm_l2;
                uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
                if (o111) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 454, 9);
                uint64_t t112 = r110;
                gm_l2 = t112;
            }
lc74:;
        }
lb74:;
    }
    {
        for (;;) {
            uint64_t t114 = gm_l4;
            uint8_t t115 = (t114 > ((uint64_t)0ULL));
            if (!t115) goto lb113;
            {
                uint64_t t118 = gm_l4;
                uint32_t t119 = gm_l1;
                unsigned __int128 n120 = (unsigned __int128)t119;
                if (n120 >= 64) gm_trap_shift((uint64_t)n120, "u64", gm_src3d0v42kavhv7, 457, 16);
                uint64_t t121 = (uint64_t)(t118 >> (int)n120);
                gm_l7 = t121;
            }
            {
                uint64_t t122 = gm_l4;
                uint64_t t123 = gm_l5;
                uint64_t t124 = (uint64_t)(t122 & t123);
                gm_l4 = t124;
            }
            {
                uint64_t t126 = gm_l3;
                uint8_t t127 = (t126 < ((uint64_t)800ULL));
                if (!t127) goto ie125;
                {
                    uint64_t t129 = gm_l3;
                    uint64_t t130 = (uintptr_t)t129;
                    void * t131 = gm_l0;
                    if (t130 >= (uintptr_t)(800)) gm_trap_index(t130, 800, gm_src3d0v42kavhv7, 460, 13);
                    uint64_t t132 = gm_l7;
                    uint8_t t133 = (uint8_t)t132;
                    GM_ST(uint8_t, ((((uint8_t *)t131) + 0) + t130 * 1), t133);
                }
                {
                    uint64_t t134 = gm_l3;
                    uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                    if (o136) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 461, 13);
                    uint64_t t137 = r135;
                    gm_l3 = t137;
                }
                goto ix125;
ie125:;
                uint64_t t139 = gm_l7;
                uint8_t t140 = (t139 > ((uint64_t)0ULL));
                if (!t140) goto ie138;
                {
                    void * t142 = gm_l0;
                    GM_ST(uint8_t, (((uint8_t *)t142) + 816), ((uint8_t)1ULL));
                }
                goto ix138;
ie138:;
ix138:;
ix125:;
            }
            {
                uint64_t t143 = gm_l4;
                uint64_t r144; bool o145 = __builtin_mul_overflow(t143, ((uint64_t)10ULL), &r144);
                if (o145) gm_trap("integer overflow in `*`", gm_src3d0v42kavhv7, 465, 9);
                uint64_t t146 = r144;
                gm_l4 = t146;
            }
lc113:;
        }
lb113:;
    }
    {
        void * t147 = gm_l0;
        uint64_t t148 = gm_l3;
        GM_ST(uint64_t, (((uint8_t *)t147) + 800), t148);
    }
    {
        void * t149 = gm_l0;
        gm_f_trim_decimal_56e5p3i(t149);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint64_t gm_f_rounded_integer_f0bb6k4(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rounded_integer_f0bb6k4 F;
    void * gm_l0; /* a */
    uint64_t gm_l1; /* dp */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 296u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        int64_t t4 = GM_LD(int64_t, (((uint8_t *)t3) + 808));
        uint8_t t5 = (t4 < ((int64_t)0LL));
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        int64_t t9 = GM_LD(int64_t, (((uint8_t *)t8) + 808));
        uint64_t t10 = (uint64_t)t9;
        gm_l1 = t10;
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t12 = gm_l3;
            uint64_t t13 = gm_l1;
            uint8_t t14 = (t12 < t13);
            if (!t14) goto lb11;
            uint64_t t15 = gm_l3;
            void * t16 = gm_l0;
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 800));
            uint8_t t18 = (t15 < t17);
            if (!t18) goto lb11;
            {
                uint64_t t21 = gm_l2;
                uint64_t r22; bool o23 = __builtin_mul_overflow(t21, ((uint64_t)10ULL), &r22);
                if (o23) gm_trap("integer overflow in `*`", gm_src3d0v42kavhv7, 539, 13);
                uint64_t t24 = r22;
                uint64_t t25 = gm_l3;
                uint64_t t26 = (uintptr_t)t25;
                void * t27 = gm_l0;
                if (t26 >= (uintptr_t)(800)) gm_trap_index(t26, 800, gm_src3d0v42kavhv7, 539, 26);
                uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t27) + 0) + t26 * 1));
                uint64_t t29 = (uint64_t)t28;
                uint64_t r30; bool o31 = __builtin_add_overflow(t24, t29, &r30);
                if (o31) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 539, 13);
                uint64_t t32 = r30;
                gm_l2 = t32;
            }
            {
                uint64_t t33 = gm_l3;
                uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                if (o35) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 540, 9);
                uint64_t t36 = r34;
                gm_l3 = t36;
            }
lc11:;
        }
lb11:;
    }
    {
        for (;;) {
            uint64_t t38 = gm_l3;
            uint64_t t39 = gm_l1;
            uint8_t t40 = (t38 < t39);
            if (!t40) goto lb37;
            {
                uint64_t t43 = gm_l2;
                uint64_t r44; bool o45 = __builtin_mul_overflow(t43, ((uint64_t)10ULL), &r44);
                if (o45) gm_trap("integer overflow in `*`", gm_src3d0v42kavhv7, 543, 9);
                uint64_t t46 = r44;
                gm_l2 = t46;
            }
            {
                uint64_t t47 = gm_l3;
                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                if (o49) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 544, 9);
                uint64_t t50 = r48;
                gm_l3 = t50;
            }
lc37:;
        }
lb37:;
    }
    {
        void * t52 = gm_l0;
        uint64_t t53 = gm_l1;
        uint8_t t54 = gm_f_should_round_up_s6nkn8b(t52, t53);
        if (!t54) goto ie51;
        {
            uint64_t t56 = gm_l2;
            uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
            if (o58) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 546, 33);
            uint64_t t59 = r57;
            gm_l2 = t59;
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        uint64_t t60 = gm_l2;
        uint8_t t61 = 0;
        F.ret = t60;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_shift_3amrqim(void * a0, int64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_shift_3amrqim F;
    void * gm_l0; /* a */
    int64_t gm_l1; /* k */
    int64_t gm_l2; /* left */
    int64_t gm_l3; /* right */
    GM_STACK_CHECK_SHALLOW(&F, 198u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 800));
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) goto ie2;
        {
            gm_b0_1 t7;
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        int64_t t9 = gm_l1;
        gm_l2 = t9;
    }
    {
        for (;;) {
            int64_t t11 = gm_l2;
            uint8_t t12 = (t11 > ((int64_t)60LL));
            if (!t12) goto lb10;
            {
                void * t15 = gm_l0;
                gm_f_left_shift_77i68r4(t15, ((uint32_t)60ULL));
            }
            {
                int64_t t16 = gm_l2;
                int64_t r17; bool o18 = __builtin_sub_overflow(t16, ((int64_t)60LL), &r17);
                if (o18) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 510, 9);
                int64_t t19 = r17;
                gm_l2 = t19;
            }
lc10:;
        }
lb10:;
    }
    {
        int64_t t21 = gm_l2;
        uint8_t t22 = (t21 > ((int64_t)0LL));
        if (!t22) goto ie20;
        {
            void * t24 = gm_l0;
            int64_t t25 = gm_l2;
            uint32_t t26 = (uint32_t)t25;
            gm_f_left_shift_77i68r4(t24, t26);
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        int64_t t27 = gm_l1;
        if (t27 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 513, 14);
        int64_t t28 = (int64_t)((uint64_t)0 - (uint64_t)t27);
        gm_l3 = t28;
    }
    {
        for (;;) {
            int64_t t30 = gm_l3;
            uint8_t t31 = (t30 > ((int64_t)60LL));
            if (!t31) goto lb29;
            {
                void * t34 = gm_l0;
                gm_f_right_shift_22mtui8(t34, ((uint32_t)60ULL));
            }
            {
                int64_t t35 = gm_l3;
                int64_t r36; bool o37 = __builtin_sub_overflow(t35, ((int64_t)60LL), &r36);
                if (o37) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 516, 9);
                int64_t t38 = r36;
                gm_l3 = t38;
            }
lc29:;
        }
lb29:;
    }
    {
        int64_t t40 = gm_l3;
        uint8_t t41 = (t40 > ((int64_t)0LL));
        if (!t41) goto ie39;
        {
            void * t43 = gm_l0;
            int64_t t44 = gm_l3;
            uint32_t t45 = (uint32_t)t44;
            gm_f_right_shift_22mtui8(t43, t45);
        }
        goto ix39;
ie39:;
ix39:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_should_round_up_s6nkn8b(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_should_round_up_s6nkn8b F;
    void * gm_l0; /* a */
    uint64_t gm_l1; /* nd */
    GM_STACK_CHECK_SHALLOW(&F, 187u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 800));
        uint8_t t6 = (t3 >= t5);
        if (!t6) goto ie2;
        {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)t10;
        void * t12 = gm_l0;
        if (t11 >= (uintptr_t)(800)) gm_trap_index(t11, 800, gm_src3d0v42kavhv7, 524, 8);
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 0) + t11 * 1));
        uint8_t t14 = ((t13 == ((uint8_t)5ULL)));
        if (!t14) goto ie9;
        uint64_t t15 = gm_l1;
        uint64_t r16; bool o17 = __builtin_add_overflow(t15, ((uint64_t)1ULL), &r16);
        if (o17) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 524, 25);
        uint64_t t18 = r16;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 800));
        uint8_t t21 = ((t18 == t20));
        if (!t21) goto ie9;
        {
            void * t24 = gm_l0;
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)t24) + 816));
            uint8_t t26 = ((t25 == ((uint8_t)1ULL)));
            if (!t26) goto ie23;
            {
                uint8_t t28 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix23;
ie23:;
ix23:;
        }
        {
            uint8_t t29 = 0;
            uint64_t t30 = gm_l1;
            uint8_t t31 = (t30 > ((uint64_t)0ULL));
            if (t31) {
                uint64_t t32 = gm_l1;
                uint64_t r33; bool o34 = __builtin_sub_overflow(t32, ((uint64_t)1ULL), &r33);
                if (o34) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 527, 31);
                uint64_t t35 = r33;
                uint64_t t36 = (uintptr_t)t35;
                void * t37 = gm_l0;
                if (t36 >= (uintptr_t)(800)) gm_trap_index(t36, 800, gm_src3d0v42kavhv7, 527, 27);
                uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t37) + 0) + t36 * 1));
                if (((uint8_t)2ULL) == 0) gm_trap("division by zero", gm_src3d0v42kavhv7, 527, 27);
                uint8_t t39 = (uint8_t)(t38 % ((uint8_t)2ULL));
                uint8_t t40 = ((t39 == ((uint8_t)1ULL)));
                t29 = t40;
            } else t29 = 0;
            uint8_t t41 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        uint64_t t42 = gm_l1;
        uint64_t t43 = (uintptr_t)t42;
        void * t44 = gm_l0;
        if (t43 >= (uintptr_t)(800)) gm_trap_index(t43, 800, gm_src3d0v42kavhv7, 529, 12);
        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t44) + 0) + t43 * 1));
        uint8_t t46 = (t45 >= ((uint8_t)5ULL));
        uint8_t t47 = 0;
        F.ret = t46;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_node *pn7 = gm_find(cx, 993381376);
            if (!pn7) gm_trap("no handler installed for effect `Yield`", gm_src3d0v42kavhv7, 139, 9);
            gm_b0_1 t8;
            t8 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn7->clauses[0])(pn7->env, pn7->hctx, pn7, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t11 = gm_l0;
        gm_l2 = t11;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb12;
            {
                gm_b16_8 t16 = gm_l2;
                gm_b16_8 t17 = gm_l1;
                gm_tf5srh4mp4b3i t18 = gm_f_find_nab29co(t16, t17);
                gm_tf5srh4mp4b3i t19 = t18;
                if (!((((uint8_t *)&t19))[8] != 0)) goto ie15;
                uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&t19));
                gm_l3 = t20;
                {
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t23 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t24 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t25 = gm_l3;
                    uint64_t t26 = (uintptr_t)t25;
                    if (t24 > t26 || t26 > (uintptr_t)t22) gm_trap_slice(t24, t26, t22, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t27;
                    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t23 + t24 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - t24));
                    gm_node *pn28 = gm_find(cx, 993381376);
                    if (!pn28) gm_trap("no handler installed for effect `Yield`", gm_src3d0v42kavhv7, 145, 13);
                    gm_b0_1 t29;
                    t29 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn28->clauses[0])(pn28->env, pn28->hctx, pn28, t27);
                    if (gm_unw.kind) {
                        goto gm_ll13;
                    }
                }
                {
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t31 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t32 = gm_l3;
                    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r34; bool o35 = __builtin_add_overflow(t32, t33, &r34);
                    if (o35) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t36 = r34;
                    uint64_t t37 = (uintptr_t)t36;
                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t39 = (uintptr_t)t38;
                    if (t37 > t39 || t39 > (uintptr_t)t30) gm_trap_slice(t37, t39, t30, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t40;
                    GM_ST(void *, (uint8_t *)&t40, (void *)(((uint8_t *)t31 + t37 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(t39 - t37));
                    gm_l2 = t40;
                }
                goto ix15;
ie15:;
                {
                    gm_b16_8 t42 = gm_l2;
                    gm_node *pn43 = gm_find(cx, 993381376);
                    if (!pn43) gm_trap("no handler installed for effect `Yield`", gm_src3d0v42kavhv7, 148, 13);
                    gm_b0_1 t44;
                    t44 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn43->clauses[0])(pn43->env, pn43->hctx, pn43, t42);
                    if (gm_unw.kind) {
                        goto gm_ll13;
                    }
                }
                {
                    gm_b0_1 t45;
                    uint8_t t46 = 0;
                    F.ret = t45;
                    goto gm_fret;
                }
ix15:;
            }
lc12:;
        }
gm_ll13:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
        }
        goto gm_lad;
lb12:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s0v7938(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_25s43ua(cx->env, cx->hctx, cx, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_25s43ua(cx->env, cx->hctx, cx, t26);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_25s43ua(cx->env, cx->hctx, cx, t40);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
gm_ll12:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
        }
        goto gm_lad;
lb11:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s1om374(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_e61ld42(cx->env, cx->hctx, cx, t6);
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_e61ld42(cx->env, cx->hctx, cx, t26);
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_e61ld42(cx->env, cx->hctx, cx, t40);
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
lb11:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s3958ap(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_3q46euu(cx->env, cx->hctx, cx, t6);
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_3q46euu(cx->env, cx->hctx, cx, t26);
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_3q46euu(cx->env, cx->hctx, cx, t40);
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
lb11:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s44sj5s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_8jjs01b(cx->env, cx->hctx, cx, t6);
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_8jjs01b(cx->env, cx->hctx, cx, t26);
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_8jjs01b(cx->env, cx->hctx, cx, t40);
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
lb11:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s5hgl7n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_kocuepv(cx->env, cx->hctx, cx, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_kocuepv(cx->env, cx->hctx, cx, t26);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_kocuepv(cx->env, cx->hctx, cx, t40);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
gm_ll12:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
        }
        goto gm_lad;
lb11:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s76o7tg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_cbj68cn(cx->env, cx->hctx, cx, t6);
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_cbj68cn(cx->env, cx->hctx, cx, t26);
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_cbj68cn(cx->env, cx->hctx, cx, t40);
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
lb11:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s7d377q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_vgf3305(cx->env, cx->hctx, cx, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_vgf3305(cx->env, cx->hctx, cx, t26);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_vgf3305(cx->env, cx->hctx, cx, t40);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
gm_ll12:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
        }
        goto gm_lad;
lb11:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s8m2rsv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_1avb45k(cx->env, cx->hctx, cx, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_1avb45k(cx->env, cx->hctx, cx, t26);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_1avb45k(cx->env, cx->hctx, cx, t40);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
gm_ll12:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
        }
        goto gm_lad;
lb11:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sbt8562(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_hb9k0f9(cx->env, cx->hctx, cx, t6);
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_hb9k0f9(cx->env, cx->hctx, cx, t26);
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_hb9k0f9(cx->env, cx->hctx, cx, t40);
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
lb11:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sf8feic(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_3msbccb(cx->env, cx->hctx, cx, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_3msbccb(cx->env, cx->hctx, cx, t26);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_3msbccb(cx->env, cx->hctx, cx, t40);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
gm_ll12:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
        }
        goto gm_lad;
lb11:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_shbdnkf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_g303nmp(cx->env, cx->hctx, cx, t6);
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_g303nmp(cx->env, cx->hctx, cx, t26);
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_g303nmp(cx->env, cx->hctx, cx, t40);
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
lb11:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sq2k3vc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_g9kn84c(cx->env, cx->hctx, cx, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_g9kn84c(cx->env, cx->hctx, cx, t26);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_g9kn84c(cx->env, cx->hctx, cx, t40);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
gm_ll12:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
        }
        goto gm_lad;
lb11:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sq3203i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_tbgr2dj(cx->env, cx->hctx, cx, t6);
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_tbgr2dj(cx->env, cx->hctx, cx, t26);
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_tbgr2dj(cx->env, cx->hctx, cx, t40);
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
lb11:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_srei23l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_6l4llq8(cx->env, cx->hctx, cx, t6);
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_6l4llq8(cx->env, cx->hctx, cx, t26);
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_6l4llq8(cx->env, cx->hctx, cx, t40);
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
lb11:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_st26gm6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_g88hj6k(cx->env, cx->hctx, cx, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_g88hj6k(cx->env, cx->hctx, cx, t26);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_g88hj6k(cx->env, cx->hctx, cx, t40);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
gm_ll12:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
        }
        goto gm_lad;
lb11:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_st80pr1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_1vrv15m(cx->env, cx->hctx, cx, t6);
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_1vrv15m(cx->env, cx->hctx, cx, t26);
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_1vrv15m(cx->env, cx->hctx, cx, t40);
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
lb11:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_stnipdi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_m8t719h(cx->env, cx->hctx, cx, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_m8t719h(cx->env, cx->hctx, cx, t26);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_m8t719h(cx->env, cx->hctx, cx, t40);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
gm_ll12:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
        }
        goto gm_lad;
lb11:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sumnih7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_6ji90d8(cx->env, cx->hctx, cx, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_6ji90d8(cx->env, cx->hctx, cx, t26);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_6ji90d8(cx->env, cx->hctx, cx, t40);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
gm_ll12:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
        }
        goto gm_lad;
lb11:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sut5g99(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_5dma72c(cx->env, cx->hctx, cx, t6);
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_5dma72c(cx->env, cx->hctx, cx, t26);
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_5dma72c(cx->env, cx->hctx, cx, t40);
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
lb11:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_svmmoet(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_i2koblr(cx->env, cx->hctx, cx, t6);
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_i2koblr(cx->env, cx->hctx, cx, t26);
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_i2koblr(cx->env, cx->hctx, cx, t40);
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
lb11:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_svn701p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_split_obtinjv F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l0;
            gm_b0_1 t7 = gm_f_for_body_v43n3ub(cx->env, cx->hctx, cx, t6);
        }
        {
            gm_b0_1 t8;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_l2 = t10;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                gm_b16_8 t15 = gm_l2;
                gm_b16_8 t16 = gm_l1;
                gm_tf5srh4mp4b3i t17 = gm_f_find_nab29co(t15, t16);
                gm_tf5srh4mp4b3i t18 = t17;
                if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
                uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t18));
                gm_l3 = t19;
                {
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t24 = gm_l3;
                    uint64_t t25 = (uintptr_t)t24;
                    if (t23 > t25 || t25 > (uintptr_t)t21) gm_trap_slice(t23, t25, t21, gm_src3d0v42kavhv7, 145, 19);
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + t23 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25 - t23));
                    gm_b0_1 t27 = gm_f_for_body_v43n3ub(cx->env, cx->hctx, cx, t26);
                }
                {
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 146, 25);
                    uint64_t t34 = r32;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t28) gm_trap_slice(t35, t37, t28, gm_src3d0v42kavhv7, 146, 20);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t29 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_l2 = t38;
                }
                goto ix14;
ie14:;
                {
                    gm_b16_8 t40 = gm_l2;
                    gm_b0_1 t41 = gm_f_for_body_v43n3ub(cx->env, cx->hctx, cx, t40);
                }
                {
                    gm_b0_1 t42;
                    uint8_t t43 = 0;
                    F.ret = t42;
                    goto gm_fret;
                }
ix14:;
            }
lc11:;
        }
lb11:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_split_once_mqcu1ce F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* sep */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 370u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_tf5srh4mp4b3i t4 = gm_f_find_nab29co(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_ttu09lij36f8c t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        gm_t6j6smj7qoctu t10;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t12 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t13 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t14 = gm_l2;
        uint64_t t15 = (uintptr_t)t14;
        if (t13 > t15 || t15 > (uintptr_t)t11) gm_trap_slice(t13, t15, t11, gm_src3d0v42kavhv7, 131, 21);
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t12 + t13 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(t15 - t13));
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t16);
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t18 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t19 = gm_l2;
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r21; bool o22 = __builtin_add_overflow(t19, t20, &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 131, 39);
        uint64_t t23 = r21;
        uint64_t t24 = (uintptr_t)t23;
        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t26 = (uintptr_t)t25;
        if (t24 > t26 || t26 > (uintptr_t)t17) gm_trap_slice(t24, t26, t17, gm_src3d0v42kavhv7, 131, 37);
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t18 + t24 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - t24));
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 16, t27);
        gm_ttu09lij36f8c t28;
        GM_ST(uint8_t, (uint8_t *)&t28 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t28 + 36, 0);
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t28), t10);
        ((uint8_t *)&t28)[32] = 1;
        uint8_t t29 = 0;
        F.ret = t28;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_ttu09lij36f8c gm_f_split_once_mqcu1ce_c(gm_b16_8 a0, gm_b16_8 a1) { gm_ttu09lij36f8c gm_r; gm_f_split_once_mqcu1ce(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_strip_prefix_b4949s8 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* prefix */
    GM_STACK_CHECK_SHALLOW(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4 = gm_l1;
        uint8_t t5 = gm_f_starts_with_6vvt01e(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            gm_th2lqbo77pvsc t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t11 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t13 = (uintptr_t)t12;
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t15 = (uintptr_t)t14;
        if (t13 > t15 || t15 > (uintptr_t)t10) gm_trap_slice(t13, t15, t10, gm_src3d0v42kavhv7, 114, 12);
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t11 + t13 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(t15 - t13));
        gm_th2lqbo77pvsc t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t17), t16);
        ((uint8_t *)&t17)[16] = 1;
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_strip_prefix_b4949s8_c(gm_b16_8 a0, gm_b16_8 a1) { gm_th2lqbo77pvsc gm_r; gm_f_strip_prefix_b4949s8(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_strip_suffix_8155gbi(void *gm_out, gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_strip_suffix_8155gbi F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* suffix */
    GM_STACK_CHECK_SHALLOW(&F, 188u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4 = gm_l1;
        uint8_t t5 = gm_f_ends_with_biriu5l(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            gm_th2lqbo77pvsc t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t11 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r15; bool o16 = __builtin_sub_overflow(t13, t14, &r15);
        if (o16) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 120, 17);
        uint64_t t17 = r15;
        uint64_t t18 = (uintptr_t)t17;
        if (t12 > t18 || t18 > (uintptr_t)t10) gm_trap_slice(t12, t18, t10, gm_src3d0v42kavhv7, 120, 12);
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t11 + t12 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(t18 - t12));
        gm_th2lqbo77pvsc t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t20), t19);
        ((uint8_t *)&t20)[16] = 1;
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_strip_suffix_8155gbi_c(gm_b16_8 a0, gm_b16_8 a1) { gm_th2lqbo77pvsc gm_r; gm_f_strip_suffix_8155gbi(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_trim_decimal_56e5p3i(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_decimal_56e5p3i F;
    void * gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 800));
            uint8_t t5 = (t4 > ((uint64_t)0ULL));
            if (!t5) goto lb2;
            void * t6 = gm_l0;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 800));
            uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)1ULL), &r8);
            if (o9) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 421, 28);
            uint64_t t10 = r8;
            uint64_t t11 = (uintptr_t)t10;
            void * t12 = gm_l0;
            if (t11 >= (uintptr_t)(800)) gm_trap_index(t11, 800, gm_src3d0v42kavhv7, 421, 24);
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 0) + t11 * 1));
            uint8_t t14 = ((t13 == ((uint8_t)0ULL)));
            if (!t14) goto lb2;
            {
                void * t17 = gm_l0;
                void * t18 = gm_l0;
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 800));
                uint64_t r20; bool o21 = __builtin_sub_overflow(t19, ((uint64_t)1ULL), &r20);
                if (o21) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 421, 45);
                uint64_t t22 = r20;
                GM_ST(uint64_t, (((uint8_t *)t17) + 800), t22);
            }
lc2:;
        }
lb2:;
    }
    {
        void * t24 = gm_l0;
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 800));
        uint8_t t26 = ((t25 == ((uint64_t)0ULL)));
        if (!t26) goto ie23;
        {
            void * t28 = gm_l0;
            GM_ST(int64_t, (((uint8_t *)t28) + 808), ((int64_t)0LL));
        }
        goto ix23;
ie23:;
ix23:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_end_6sjv6rg F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 180u);
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
            uint64_t t6 = gm_l1;
            uint64_t r7; bool o8 = __builtin_sub_overflow(t6, ((uint64_t)1ULL), &r7);
            if (o8) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 37, 38);
            uint64_t t9 = r7;
            uint64_t t10 = (uintptr_t)t9;
            uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_src3d0v42kavhv7, 37, 36);
            uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t10 * 1));
            uint8_t t13 = gm_f_is_space_uen38t5(t12);
            if (!t13) goto lb3;
            {
                uint64_t t16 = gm_l1;
                uint64_t r17; bool o18 = __builtin_sub_overflow(t16, ((uint64_t)1ULL), &r17);
                if (o18) gm_trap("integer overflow in `-`", gm_src3d0v42kavhv7, 37, 48);
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
        if (t22 > t24 || t24 > (uintptr_t)t20) gm_trap_slice(t22, t24, t20, gm_src3d0v42kavhv7, 38, 12);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_start_179crak F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 180u);
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
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_src3d0v42kavhv7, 30, 40);
            uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t7 * 1));
            uint8_t t10 = gm_f_is_space_uen38t5(t9);
            if (!t10) goto lb2;
            {
                uint64_t t13 = gm_l1;
                uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)1ULL), &r14);
                if (o15) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 30, 48);
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
        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t22 = (uintptr_t)t21;
        if (t20 > t22 || t22 > (uintptr_t)t17) gm_trap_slice(t20, t22, t17, gm_src3d0v42kavhv7, 31, 12);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t18 + t20 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22 - t20));
        uint8_t t24 = 0;
        F.ret = t23;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
