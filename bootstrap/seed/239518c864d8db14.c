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
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f1; } gm_ttsm01t0d8srg;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint8_t f3; uint8_t p4[3]; uint32_t f4; } gm_t84tkl8vf34ko;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttsm01t0d8srg) == 1 && _Alignof(gm_ttsm01t0d8srg) == 1 && offsetof(gm_ttsm01t0d8srg, f1) == 0, "layout of `?()`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t84tkl8vf34ko) == 56 && _Alignof(gm_t84tkl8vf34ko) == 8 && offsetof(gm_t84tkl8vf34ko, f0) == 0 && offsetof(gm_t84tkl8vf34ko, f1) == 16 && offsetof(gm_t84tkl8vf34ko, f2) == 32 && offsetof(gm_t84tkl8vf34ko, f3) == 48 && offsetof(gm_t84tkl8vf34ko, f4) == 52, "layout of `Decl`");
typedef struct { uint64_t f0; gm_t84tkl8vf34ko f1; } gm_t9re6l0g6jicn;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint64_t f0; gm_trqfvmjtt4sij f1; } gm_tmupb6k0asuti;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_t9re6l0g6jicn) == 64 && _Alignof(gm_t9re6l0g6jicn) == 8 && offsetof(gm_t9re6l0g6jicn, f0) == 0 && offsetof(gm_t9re6l0g6jicn, f1) == 8, "layout of `(i: usize, x: Decl)`");
_Static_assert(sizeof(gm_tmupb6k0asuti) == 48 && _Alignof(gm_tmupb6k0asuti) == 8 && offsetof(gm_tmupb6k0asuti, f0) == 0 && offsetof(gm_tmupb6k0asuti, f1) == 8, "layout of `(i: usize, x: List((name: string, def: string)))`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
struct gm_fr_brace_balance_aofv41t {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_c_fn_name_77uspqp {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_code_bytes_u2ir6qv {
    gm_frame h;
    uint64_t l1; /* n */
    uint64_t ret;
    gm_b0_1 w0;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_decl_before_fp800ft {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_decl_items_l330ksi {
    gm_frame h;
    gm_b16_8 l0; /* text */
    gm_trqfvmjtt4sij l1; /* items */
    uint64_t l2; /* start */
    int32_t l3; /* depth */
    uint64_t l4; /* i */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_declared_name_hovvm2b {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_decls_of_srep9du {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1di25gg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_465e48m {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7hq6cu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jckt05t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n7sb6c8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tatvtbf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tklonjv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vt6lar6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_k51ik07 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_vogsd72 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_external_f65nm8q {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fn_575gb0c {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_6gkpg0v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_idfg0st {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_69vpqmq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_fe8qj55 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_j11vulv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_keqa9hq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_n9765ef {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_ui0bpsf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_vn00ddp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_function_o2lv8g6 {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_get_ece4opn {
    gm_frame h;
    gm_ttsm01t0d8srg ret;
};
struct gm_fr_get_ptr_53uc4fm {
    gm_frame h;
    void * ret;
};
struct gm_fr_gm_names_6ner0lu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_t4c7ipjj9s3fp l2; /* seen */
    gm_b16_8 ret;
};
struct gm_fr_has_word_l11ltcj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_dklib0j {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_lljdp3h {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_m04clhi {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_ident_char_pmpasvq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_ident_set_9ajalpn {
    gm_frame h;
    gm_t4c7ipjj9s3fp l1; /* set */
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_inline_definition_6aokb9d {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_is_alnum_n81ervr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_alpha_12215ga {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_digit_2hoggnj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ident_cont_317fpdb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_inline_aqdknmj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_lower_4ebokoh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_upper_iekrftp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_items_of_q103oiv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* items */
    gm_th2lqbo77pvsc ret;
    gm_b0_1 w5;
};
struct gm_fr_line_reset_73tgnor {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_list_016fo30 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1g734tk {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_b7l80f8 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_c8kt75n {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_lj5vg1g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_optimized_unit_count_5mur81l {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_pop_vr08sii {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_push_0ek3ea4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_97b8r3k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rda4o9n {
    gm_frame h;
    gm_t84tkl8vf34ko l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rlpbnuq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vnejmrp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_46rs2i6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_7c10m5i {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_e4d8f0p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_qpqf461 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_vefhvf2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_set_depths_1781b1p {
    gm_frame h;
    gm_t84tkl8vf34ko l5; /* d */
    gm_trqfvmjtt4sij l10; /* l */
    gm_b0_1 ret;
};
struct gm_fr_skip_literal_17lq2p8 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_sort_by_08k4kf2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sort_by_4j6ktaf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sort_name_ro82p6b {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_sort_skeree7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_split_by_file_hgjqgsp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* decls */
    gm_t4c7ipjj9s3fp l2; /* inline_names */
    gm_trqfvmjtt4sij l3; /* bodies */
    gm_trqfvmjtt4sij l7; /* fns */
    gm_trqfvmjtt4sij l15; /* externs */
    gm_trqfvmjtt4sij l19; /* groups */
    gm_t4c7ipjj9s3fp l23; /* by_name */
    gm_trqfvmjtt4sij l24; /* nameless */
    gm_t84tkl8vf34ko l28; /* d */
    gm_trqfvmjtt4sij l30; /* l */
    gm_trqfvmjtt4sij l31; /* units */
    gm_trqfvmjtt4sij l35; /* g */
    gm_trqfvmjtt4sij l36; /* text */
    gm_trqfvmjtt4sij l43; /* fns */
    gm_trqfvmjtt4sij l44; /* ext */
    gm_trqfvmjtt4sij l45; /* all */
    gm_trqfvmjtt4sij l46; /* body */
    gm_ttu09lij36f8c ret;
    gm_b0_1 w3;
    gm_b0_1 w5;
};
struct gm_fr_split_jendjdm {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_split_shared_a91530r {
    gm_frame h;
    gm_t4c7ipjj9s3fp l3; /* inline_names */
    gm_trqfvmjtt4sij l4; /* inline_defs */
    gm_trqfvmjtt4sij l9; /* decls */
    gm_trqfvmjtt4sij l10; /* defs */
    gm_trqfvmjtt4sij l11; /* externs */
    gm_trqfvmjtt4sij l14; /* items */
    gm_trqfvmjtt4sij l25; /* units */
    gm_trqfvmjtt4sij l26; /* first */
    gm_trqfvmjtt4sij l31; /* out */
    gm_trqfvmjtt4sij l34; /* u */
    gm_ttu09lij36f8c ret;
    gm_b0_1 w1;
    gm_b0_1 w2;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_count_dk0d2e4 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_unit_text_mmejmp8 {
    gm_frame h;
    gm_b16_8 l0; /* decls */
    gm_trqfvmjtt4sij l5; /* taken */
    gm_t4c7ipjj9s3fp l8; /* defined */
    gm_trqfvmjtt4sij l9; /* work */
    gm_trqfvmjtt4sij l11; /* l */
    gm_trqfvmjtt4sij l26; /* chosen */
    gm_trqfvmjtt4sij l31; /* text */
    gm_t4c7ipjj9s3fp l32; /* seen */
    gm_trqfvmjtt4sij l36; /* used */
    gm_t4c7ipjj9s3fp l37; /* idents */
    gm_b16_8 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) int32_t gm_f_brace_balance_aofv41t(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_c_fn_name_77uspqp(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_c_fn_name_77uspqp_c(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_code_bytes_u2ir6qv(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_decl_before_fp800ft(void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_decl_items_l330ksi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_declared_name_hovvm2b(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_decls_of_srep9du(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_deinit_lehld1n(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_nvuvq05(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_vtu6170(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1di25gg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_465e48m(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7hq6cu2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n7sb6c8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tatvtbf(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tklonjv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vt6lar6(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_k51ik07(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_external_f65nm8q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_heobcrg(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_sktqt7b(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_575gb0c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_idfg0st(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_69vpqmq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fe8qj55(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_j11vulv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_keqa9hq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_n9765ef(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ui0bpsf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vn00ddp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_1u4jnnu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_69nn7l3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8l8mb6d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c2lphrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dg0m8l8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q8i7gsb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rse2efa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_stmnn5h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_u8fgvjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_uaq30dv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_function_o2lv8g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_get_ece4opn(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_ohh8mo3(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_get_ptr_53uc4fm(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_gm_names_6ner0lu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_guard_o7k9a0e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_has_word_l11ltcj(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_hash64_apkb7bo(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_dklib0j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_lljdp3h(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_ident_char_pmpasvq(uint8_t a0);
__attribute__((aligned(4))) void gm_f_ident_set_9ajalpn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_inline_definition_6aokb9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_introsort_3q5sbn1(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_6k4m7ds(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_r6p6ckl(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_inline_aqdknmj(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0);
__attribute__((aligned(4))) void gm_f_items_of_q103oiv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_line_reset_73tgnor(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s1f7rak(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s1hv6k0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sa157kc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sfohpq4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_snvhnj5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_sp8kip0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_svejqbu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_016fo30(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1g734tk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_b7l80f8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_c8kt75n(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_lj5vg1g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint64_t gm_f_optimized_unit_count_5mur81l(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0ek3ea4(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_97b8r3k(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rda4o9n(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rlpbnuq(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vnejmrp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7o5vpta(void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_drsjd1t(void * a0, gm_b16_8 a1, gm_b0_1 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qoojoqv(void * a0, gm_b16_8 a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_46rs2i6(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7c10m5i(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_e4d8f0p(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_qpqf461(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vefhvf2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_replace_alloc_t7hndlq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_set_depths_1781b1p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) uint64_t gm_f_skip_literal_17lq2p8(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_sort_by_08k4kf2(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_sort_by_4j6ktaf(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sort_name_ro82p6b(void * a0);
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_split_by_file_hgjqgsp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_split_jendjdm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_split_shared_a91530r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_suffix_8155gbi(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_unit_count_dk0d2e4(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_unit_text_mmejmp8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_815rgbg(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cdouvhd(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_d8c7tpm(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qm3m7a0(void * a0, const void * a1);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_c30tdq2p48i[1] __attribute__((aligned(1)));
extern uint8_t gm_c4crumb5mc9[1] __attribute__((aligned(1)));
extern uint8_t gm_c72j1g8d455[1] __attribute__((aligned(1)));
extern uint8_t gm_ccmgjp6th6r[16] __attribute__((aligned(8)));
extern uint8_t gm_cos4c858gsq[1] __attribute__((aligned(1)));
extern uint8_t gm_crpb2rekieb[1] __attribute__((aligned(1)));
extern uint8_t gm_csu9mkbsfpo[16] __attribute__((aligned(8)));
extern void *const gm_hl1l1s81m49k[];
extern void *const gm_hl8sdnavrdjn[];
extern void *const gm_hlc7jtja1te5[];
extern void *const gm_hlrddq2io9s1[];
extern void *const gm_hlth8t37utgd[];
extern void *const gm_hlu84m640fm9[];
extern void *const gm_hlvtf175jpmc[];
extern const uint8_t gm_s1oiqt105ulvb[20];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s7ipvt5onv6ht[4];
extern const uint8_t gm_saalafjdd3tnp[9];
extern const uint8_t gm_sao4mumvadoct[4];
extern const uint8_t gm_sasea0sobfkrs[8];
extern const uint8_t gm_sd1uj6gf5v8qh[8];
extern const uint8_t gm_seccpktgmrevt[2];
extern const uint8_t gm_shv3ttlq9l359[15];
extern const uint8_t gm_so0h0lkl6bp4l[39];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so7qmna0fo4so[6];
extern const uint8_t gm_soa9mnl4ahjud[8];
extern const uint8_t gm_spkrf9e7cr6h4[7];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srccfhjr4r9mdef[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ssi4rrma2q00g[7];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) uint64_t gm_f_code_bytes_u2ir6qv(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_code_bytes_u2ir6qv F;
    gm_b16_8 gm_l0; /* item */
    gm_b16_8 gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        F.l1 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlvtf175jpmc, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l2;
        gm_b0_1 t9 = gm_f_lines_9uns0k2_sfohpq4((void *)NULL, (&wn5), NULL, t8);
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
        uint64_t t13 = F.l1;
        uint8_t t14 = 0;
        F.ret = t13;
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_find_nab29co(t1, t2);
    uint8_t t4;
    if (((((uint8_t *)&t3))[8] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 84, 50);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1di25gg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1di25gg F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_465e48m(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_465e48m F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7hq6cu2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7hq6cu2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jckt05t F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n7sb6c8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n7sb6c8 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tatvtbf(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tatvtbf F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tklonjv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tklonjv F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 64)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vt6lar6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vt6lar6 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_0cs85gs F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
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
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_3cp9o8d(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 1);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_k51ik07(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_k51ik07 F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
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
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_e4d8f0p(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 32)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 32);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_vogsd72 F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
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
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_0jmnnpk(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 16);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_575gb0c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_575gb0c F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, ((uint8_t *)t1));
    uint64_t t3 = (uintptr_t)t2;
    uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_unit_text_mmejmp8 *)((&F.h)->up))->l0) + 8);
    if (t3 >= (uintptr_t)(t4)) gm_trap_index(t3, t4, gm_srccfhjr4r9mdef, 683, 73);
    void * t5 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&((struct gm_fr_unit_text_mmejmp8 *)((&F.h)->up))->l0)) + t3 * 56);
    void * t6 = gm_l1;
    uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)t6));
    uint64_t t8 = (uintptr_t)t7;
    uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_unit_text_mmejmp8 *)((&F.h)->up))->l0) + 8);
    if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srccfhjr4r9mdef, 683, 85);
    void * t10 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&((struct gm_fr_unit_text_mmejmp8 *)((&F.h)->up))->l0)) + t8 * 56);
    uint8_t t11 = gm_f_decl_before_fp800ft(t5, t10);
    F.ret = t11;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_6gkpg0v F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)t1));
    void * t3 = gm_l1;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)t3));
    uint8_t t5 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t2), ((uint8_t *)&t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_idfg0st(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_idfg0st F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 167u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
    void * t4 = gm_l1;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 0));
    uint8_t t6 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t3), ((uint8_t *)&t5));
    if (!t6) {
        uint8_t t7 = 0;
        void * t8 = gm_l0;
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 0));
        void * t10 = gm_l1;
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 0));
        uint8_t t12 = (gm_eq0iqiubfm7e(((uint8_t *)&t9), ((uint8_t *)&t11)));
        if (t12) {
            void * t13 = gm_l0;
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 16));
            void * t15 = gm_l1;
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 16));
            uint8_t t17 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t14), ((uint8_t *)&t16));
            t7 = t17;
        } else t7 = 0;
        t1 = t7;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_get_ece4opn(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ece4opn F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_sktqt7b(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_ttsm01t0d8srg t7;
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
        gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 24) + 24));
        gm_ttsm01t0d8srg t14;
        GM_ST(gm_b0_1, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[0] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_get_ptr_53uc4fm(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ptr_53uc4fm F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
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
        gm_tf5srh4mp4b3i t4 = gm_f_find_heobcrg(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            uint8_t t7 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        uint64_t t8 = gm_l2;
        uint64_t t9 = (uintptr_t)t8;
        void * t10 = gm_l0;
        uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 0) + 8);
        if (t9 >= (uintptr_t)(t11)) gm_trap_index(t9, t11, gm_srcbsc23is03ttm, 127, 13);
        void * t12 = (void *)(((uint8_t *)GM_LD(void *, (((uint8_t *)t10) + 0)) + t9 * 64) + 24);
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_dklib0j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_dklib0j F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_tklonjv();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_lljdp3h(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_lljdp3h F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_7hq6cu2();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_m04clhi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_jckt05t();
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
inline __attribute__((aligned(4))) uint8_t gm_f_ident_char_pmpasvq(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ident_char_pmpasvq F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK(&F, 5u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    uint8_t t2 = gm_f_is_ident_cont_317fpdb(t1);
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_alnum_n81ervr F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_alpha_12215ga(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_digit_2hoggnj(t4);
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_alpha_12215ga F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_upper_iekrftp(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_lower_4ebokoh(t4);
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_digit_2hoggnj F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)48ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)57ULL));
        t1 = t5;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ident_cont_317fpdb F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_alnum_n81ervr(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = ((t4 == ((uint8_t)95ULL)));
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_inline_aqdknmj(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_inline_aqdknmj F;
    gm_b16_8 gm_l0; /* line */
    GM_STACK_CHECK_SHALLOW(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soa9mnl4ahjud));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    uint8_t t4 = gm_f_starts_with_6vvt01e(t2, t3);
    if (t4) {
        gm_b16_8 t5 = gm_l0;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_so0h0lkl6bp4l));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(38));
        uint8_t t7 = gm_f_contains_gkki4kq(t5, t6);
        t1 = t7;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_lower_4ebokoh F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)97ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)122ULL));
        t1 = t5;
    } else t1 = 0;
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
inline __attribute__((aligned(4))) void gm_f_list_016fo30(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_016fo30 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_n7sb6c8();
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
inline __attribute__((aligned(4))) void gm_f_list_1g734tk(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1g734tk F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1di25gg();
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
inline __attribute__((aligned(4))) void gm_f_list_b7l80f8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_b7l80f8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vt6lar6();
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
inline __attribute__((aligned(4))) void gm_f_list_c8kt75n(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_c8kt75n F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_tatvtbf();
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
inline __attribute__((aligned(4))) void gm_f_list_lj5vg1g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_lj5vg1g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_465e48m();
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
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s83jn2q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s1lbe5s();
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_b16_8 t29 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t29);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0ek3ea4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0ek3ea4 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_tatvtbf();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_e4d8f0p(t10, ((uint64_t)1ULL), t12);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_97b8r3k(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_97b8r3k F;
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
            gm_b16_8 t11 = gm_f_empty_slice_465e48m();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_vefhvf2(t10, ((uint64_t)1ULL), t12);
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
        gm_trqfvmjtt4sij t21 = F.l1;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)t20 + t18 * 40), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rda4o9n(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rda4o9n F;
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
            gm_b16_8 t11 = gm_f_empty_slice_n7sb6c8();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_46rs2i6(t10, ((uint64_t)1ULL), t12);
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
        gm_t84tkl8vf34ko t21 = GM_LD(gm_t84tkl8vf34ko, ((uint8_t *)a1));
        GM_ST(gm_t84tkl8vf34ko, ((uint8_t *)t20 + t18 * 56), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 56)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rlpbnuq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rlpbnuq F;
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
            gm_b16_8 t11 = gm_f_empty_slice_1di25gg();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_qpqf461(t10, ((uint64_t)1ULL), t12);
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
        gm_trqfvmjtt4sij t21 = F.l1;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)t20 + t18 * 40), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t83rg2m F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_ijtp9lo();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_3cp9o8d(t10, ((uint64_t)1ULL), t12);
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
        uint8_t t21 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t20 + t18 * 1), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vd1290p F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_s1lbe5s();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_hkmvuvc(t10, ((uint64_t)1ULL), t12);
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
        uint8_t t21 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t20 + t18 * 1), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vnejmrp(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vnejmrp F;
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
            gm_b16_8 t11 = gm_f_empty_slice_vt6lar6();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_7c10m5i(t10, ((uint64_t)1ULL), t12);
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
        gm_trqfvmjtt4sij t21 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)a1));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)t20 + t18 * 40), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 40)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3cp9o8d F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_46rs2i6(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_46rs2i6 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7c10m5i(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_7c10m5i F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_e4d8f0p(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_e4d8f0p F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hkmvuvc F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_qpqf461(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_qpqf461 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vefhvf2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_vefhvf2 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sort_by_08k4kf2(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_08k4kf2 F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = gm_l1;
        gm_f_introsort_6k4m7ds(t2, t3);
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_sort_by_4j6ktaf(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_4j6ktaf F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = gm_l1;
        gm_f_introsort_3q5sbn1(t2, t3);
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_skeree7 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = ((gm_closure){GM_PTR_TAG(gm_f_fn_6gkpg0v), NULL, cx});
        gm_f_introsort_r6p6ckl(t2, t3);
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_6vvt01e F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
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
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t12 = (uintptr_t)t11;
        if (t10 > t12 || t12 > (uintptr_t)t8) gm_trap_slice(t10, t12, t8, gm_srcc24pnihhgkug, 416, 12);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t9 + t10 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t10));
        gm_b16_8 t14 = gm_l1;
        uint8_t t15 = (gm_eq0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t14)));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) uint64_t gm_f_unit_count_dk0d2e4(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unit_count_dk0d2e4 F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* cpus */
    gm_tf5srh4mp4b3i gm_l2; /* forced */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie2;
        uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        gm_l3 = t3;
        {
            uint64_t t5 = gm_l3;
            uint64_t t6 = (t5 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t5 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t5));
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 64) + 8);
        if (((uint64_t)98304ULL) == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 82, 13);
        uint64_t t10 = (uint64_t)(t9 / ((uint64_t)98304ULL));
        uint64_t t11 = gm_l1;
        uint64_t t12 = (t11 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t11 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t11));
        uint64_t t13 = (t10 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t10 > t12 ? t12 : t10));
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
inline __attribute__((aligned(4))) int32_t gm_f_brace_balance_aofv41t(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_brace_balance_aofv41t F;
    gm_b16_8 gm_l0; /* line */
    int32_t gm_l1; /* d */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((int32_t)0LL);
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
                uint64_t t9 = (uintptr_t)t8;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srccfhjr4r9mdef, 414, 14);
                uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
                gm_l3 = t11;
            }
            {
                uint8_t t13 = 0;
                uint8_t t14 = gm_l3;
                uint8_t t15 = ((t14 == ((uint8_t)34ULL)));
                if (!t15) {
                    uint8_t t16 = gm_l3;
                    uint8_t t17 = ((t16 == ((uint8_t)39ULL)));
                    t13 = t17;
                } else t13 = 1;
                if (!t13) goto ie12;
                {
                    gm_b16_8 t19 = gm_l0;
                    uint64_t t20 = gm_l2;
                    uint64_t t21 = gm_f_skip_literal_17lq2p8(t19, t20);
                    gm_l2 = t21;
                }
                {
                    goto lc2;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                uint8_t t23 = gm_l3;
                uint8_t t24 = ((t23 == ((uint8_t)123ULL)));
                if (!t24) goto ie22;
                {
                    int32_t t26 = gm_l1;
                    int32_t r27; bool o28 = __builtin_add_overflow(t26, ((int32_t)1LL), &r27);
                    if (o28) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 419, 23);
                    int32_t t29 = r27;
                    gm_l1 = t29;
                }
                goto ix22;
ie22:;
                uint8_t t31 = gm_l3;
                uint8_t t32 = ((t31 == ((uint8_t)125ULL)));
                if (!t32) goto ie30;
                {
                    int32_t t34 = gm_l1;
                    int32_t r35; bool o36 = __builtin_sub_overflow(t34, ((int32_t)1LL), &r35);
                    if (o36) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 419, 51);
                    int32_t t37 = r35;
                    gm_l1 = t37;
                }
                goto ix30;
ie30:;
ix30:;
ix22:;
            }
            {
                uint64_t t38 = gm_l2;
                uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                if (o40) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 420, 9);
                uint64_t t41 = r39;
                gm_l2 = t41;
            }
lc2:;
        }
lb2:;
    }
    {
        int32_t t42 = gm_l1;
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_c_fn_name_77uspqp(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_c_fn_name_77uspqp F;
    gm_b16_8 gm_l0; /* line */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* j */
    GM_STACK_CHECK(&F, 398u);
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
            uint64_t r4; bool o5 = __builtin_add_overflow(t3, ((uint64_t)3ULL), &r4);
            if (o5) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 176, 11);
            uint64_t t6 = r4;
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t8 = (t6 <= t7);
            if (!t8) goto lb2;
            {
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t13 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t14 = gm_l1;
                uint64_t t15 = (uintptr_t)t14;
                uint64_t t16 = gm_l1;
                uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)3ULL), &r17);
                if (o18) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 177, 20);
                uint64_t t19 = r17;
                uint64_t t20 = (uintptr_t)t19;
                if (t15 > t20 || t20 > (uintptr_t)t12) gm_trap_slice(t15, t20, t12, gm_srccfhjr4r9mdef, 177, 12);
                gm_b16_8 t21;
                GM_ST(void *, (uint8_t *)&t21, (void *)(((uint8_t *)t13 + t15 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(t20 - t15));
                uint8_t t22 = (((GM_LD(uintptr_t, ((uint8_t *)&t21) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&t21)), gm_s7ipvt5onv6ht, 3) == 0));
                if (!t22) goto ie11;
                uint8_t t23 = 0;
                uint64_t t24 = gm_l1;
                uint8_t t25 = ((t24 == ((uint64_t)0ULL)));
                if (!t25) {
                    uint64_t t26 = gm_l1;
                    uint64_t r27; bool o28 = __builtin_sub_overflow(t26, ((uint64_t)1ULL), &r27);
                    if (o28) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 177, 81);
                    uint64_t t29 = r27;
                    uint64_t t30 = (uintptr_t)t29;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srccfhjr4r9mdef, 177, 76);
                    uint8_t t32 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t30 * 1));
                    uint8_t t33 = gm_f_is_ident_cont_317fpdb(t32);
                    uint8_t t34 = (uint8_t)!t33;
                    t23 = t34;
                } else t23 = 1;
                if (!t23) goto ie11;
                {
                    uint64_t t36 = gm_l1;
                    gm_l2 = t36;
                }
                {
                    for (;;) {
                        uint64_t t38 = gm_l2;
                        uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t40 = (t38 < t39);
                        if (!t40) goto lb37;
                        uint64_t t41 = gm_l2;
                        uint64_t t42 = (uintptr_t)t41;
                        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_srccfhjr4r9mdef, 179, 57);
                        uint8_t t44 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t42 * 1));
                        uint8_t t45 = gm_f_is_ident_cont_317fpdb(t44);
                        if (!t45) goto lb37;
                        {
                            uint64_t t48 = gm_l2;
                            uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                            if (o50) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 179, 68);
                            uint64_t t51 = r49;
                            gm_l2 = t51;
                        }
lc37:;
                    }
lb37:;
                }
                {
                    uint64_t t53 = gm_l2;
                    uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t55 = (t53 < t54);
                    if (!t55) goto ie52;
                    uint64_t t56 = gm_l2;
                    uint64_t t57 = (uintptr_t)t56;
                    uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t57 >= (uintptr_t)(t58)) gm_trap_index(t57, t58, gm_srccfhjr4r9mdef, 180, 33);
                    uint8_t t59 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t57 * 1));
                    uint8_t t60 = ((t59 == ((uint8_t)40ULL)));
                    if (!t60) goto ie52;
                    {
                        uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        void * t63 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                        uint64_t t64 = gm_l1;
                        uint64_t t65 = (uintptr_t)t64;
                        uint64_t t66 = gm_l2;
                        uint64_t t67 = (uintptr_t)t66;
                        if (t65 > t67 || t67 > (uintptr_t)t62) gm_trap_slice(t65, t67, t62, gm_srccfhjr4r9mdef, 180, 57);
                        gm_b16_8 t68;
                        GM_ST(void *, (uint8_t *)&t68, (void *)(((uint8_t *)t63 + t65 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(t67 - t65));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t68);
                        goto gm_fdone;
                    }
                    goto ix52;
ie52:;
ix52:;
                }
                {
                    uint64_t t69 = gm_l2;
                    gm_l1 = t69;
                }
                goto ix11;
ie11:;
                {
                    uint64_t t71 = gm_l1;
                    uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                    if (o73) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 183, 13);
                    uint64_t t74 = r72;
                    gm_l1 = t74;
                }
ix11:;
            }
lc2:;
        }
lb2:;
    }
    {
        gm_th2lqbo77pvsc t75;
        memset(&t75, 0, sizeof t75);
        uint8_t t76 = 0;
        F.ret = t75;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_c_fn_name_77uspqp_c(gm_b16_8 a0) { gm_th2lqbo77pvsc gm_r; gm_f_c_fn_name_77uspqp(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) uint64_t gm_f_code_bytes_u2ir6qv(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_before_fp800ft(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_before_fp800ft F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    gm_b16_8 gm_l2; /* an */
    gm_b16_8 gm_l3; /* bn */
    GM_STACK_CHECK_SHALLOW(&F, 358u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 48));
        void * t5 = gm_l1;
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t5) + 48));
        uint8_t t7 = (!(t4 == t6));
        if (!t7) goto ie2;
        {
            void * t9 = gm_l0;
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t9) + 48));
            void * t11 = gm_l1;
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t11) + 48));
            uint8_t t13 = (t10 < t12);
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t16 = gm_l0;
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t16) + 52));
        void * t18 = gm_l1;
        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)t18) + 52));
        uint8_t t20 = (!(t17 == t19));
        if (!t20) goto ie15;
        {
            void * t22 = gm_l0;
            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)t22) + 52));
            void * t24 = gm_l1;
            uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)t24) + 52));
            uint8_t t26 = (t23 < t25);
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        void * t28 = gm_l0;
        gm_b16_8 t29 = gm_f_sort_name_ro82p6b(t28);
        gm_l2 = t29;
    }
    {
        void * t30 = gm_l1;
        gm_b16_8 t31 = gm_f_sort_name_ro82p6b(t30);
        gm_l3 = t31;
    }
    {
        gm_b16_8 t33 = gm_l2;
        gm_b16_8 t34 = gm_l3;
        uint8_t t35 = (!gm_eq0iqiubfm7e(((uint8_t *)&t33), ((uint8_t *)&t34)));
        if (!t35) goto ie32;
        {
            gm_b16_8 t37 = gm_l2;
            gm_b16_8 t38 = gm_l3;
            uint8_t t39 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t37), ((uint8_t *)&t38));
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        void * t42 = gm_l0;
        uint64_t t43 = GM_LD(uintptr_t, (((uint8_t *)t42) + 16) + 8);
        uint8_t t44 = ((t43 == ((uint64_t)0ULL)));
        void * t45 = gm_l1;
        uint64_t t46 = GM_LD(uintptr_t, (((uint8_t *)t45) + 16) + 8);
        uint8_t t47 = ((t46 == ((uint64_t)0ULL)));
        uint8_t t48 = (!(t44 == t47));
        if (!t48) goto ie41;
        {
            void * t50 = gm_l1;
            uint64_t t51 = GM_LD(uintptr_t, (((uint8_t *)t50) + 16) + 8);
            uint8_t t52 = ((t51 == ((uint64_t)0ULL)));
            uint8_t t53 = 0;
            F.ret = t52;
            goto gm_fret;
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        void * t54 = gm_l0;
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)t54) + 0));
        void * t56 = gm_l1;
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)t56) + 0));
        uint8_t t58 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t55), ((uint8_t *)&t57));
        uint8_t t59 = 0;
        F.ret = t58;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_decl_items_l330ksi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_decl_items_l330ksi F;
    gm_b16_8 gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 293u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_f_list_epvducg((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        F.l2 = ((uint64_t)0ULL);
    }
    {
        F.l3 = ((int32_t)0LL);
    }
    {
        F.l4 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t3 = F.l0;
            gm_l5 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlu84m640fm9, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l5;
        gm_b0_1 t9 = gm_f_lines_9uns0k2_sa157kc((void *)NULL, (&wn5), NULL, t8);
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
        uint64_t t14 = F.l2;
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
        uint8_t t16 = (t14 < t15);
        if (!t16) goto ie13;
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
        void * t18 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l0)));
        uint64_t t19 = F.l2;
        uint64_t t20 = (uintptr_t)t19;
        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
        uint64_t t22 = (uintptr_t)t21;
        if (t20 > t22 || t22 > (uintptr_t)t17) gm_trap_slice(t20, t22, t17, gm_srccfhjr4r9mdef, 405, 38);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t18 + t20 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22 - t20));
        gm_b16_8 t24 = gm_f_trim_fdrfe6i(t23);
        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&t24) + 8);
        uint8_t t26 = (t25 > ((uint64_t)0ULL));
        if (!t26) goto ie13;
        {
            void * t28 = (void *)((uint8_t *)&F.l1);
            uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
            void * t30 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l0)));
            uint64_t t31 = F.l2;
            uint64_t t32 = (uintptr_t)t31;
            uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
            uint64_t t34 = (uintptr_t)t33;
            if (t32 > t34 || t34 > (uintptr_t)t29) gm_trap_slice(t32, t34, t29, gm_srccfhjr4r9mdef, 405, 82);
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(((uint8_t *)t30 + t32 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(t34 - t32));
            gm_tftb1odc739t5 t36 = gm_f_push_3ef746b(t28, t35);
            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srccfhjr4r9mdef, 405, 71);
            gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t39 = 0;
        F.ret = t38;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_declared_name_hovvm2b(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_declared_name_hovvm2b F;
    gm_b16_8 gm_l0; /* item */
    gm_b16_8 gm_l1; /* text */
    uint64_t gm_l2; /* nl */
    gm_b16_8 gm_l3; /* rest */
    uint64_t gm_l4; /* j */
    gm_b16_8 gm_l5; /* name */
    int32_t gm_l6; /* depth */
    uint64_t gm_l7; /* i */
    uint8_t gm_l8; /* c */
    uint64_t gm_l9; /* j */
    GM_STACK_CHECK(&F, 1004u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_l1 = t2;
    }
    {
        for (;;) {
            gm_b16_8 t4 = gm_l1;
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_so7qmna0fo4so));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(5));
            uint8_t t6 = gm_f_starts_with_6vvt01e(t4, t5);
            if (!t6) goto lb3;
            {
                gm_b16_8 t9 = gm_l1;
                gm_b16_8 t10;
                GM_ST(void *, (uint8_t *)&t10, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(1));
                gm_tf5srh4mp4b3i t11 = gm_f_find_nab29co(t9, t10);
                uint64_t t12;
                if (((((uint8_t *)&t11))[8] != 0)) {
                    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&t11) + 0));
                    t12 = t13;
                } else {
                    gm_b16_8 t14;
                    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(0));
                    uint8_t t15 = 0;
                    F.ret = t14;
                    goto gm_fret;
                }
                gm_l2 = t12;
            }
            {
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                void * t17 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                uint64_t t18 = gm_l2;
                uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
                if (o20) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 473, 21);
                uint64_t t21 = r19;
                uint64_t t22 = (uintptr_t)t21;
                uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint64_t t24 = (uintptr_t)t23;
                if (t22 > t24 || t24 > (uintptr_t)t16) gm_trap_slice(t22, t24, t16, gm_srccfhjr4r9mdef, 473, 16);
                gm_b16_8 t25;
                GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t17 + t22 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24 - t22));
                gm_l1 = t25;
            }
lc3:;
        }
lb3:;
    }
    {
        gm_b16_8 t27 = gm_l1;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_saalafjdd3tnp));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(8));
        uint8_t t29 = gm_f_starts_with_6vvt01e(t27, t28);
        if (!t29) goto ie26;
        {
            uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            void * t32 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(gm_saalafjdd3tnp));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(8));
            uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&t33) + 8);
            uint64_t t35 = (uintptr_t)t34;
            uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t t37 = (uintptr_t)t36;
            if (t35 > t37 || t37 > (uintptr_t)t31) gm_trap_slice(t35, t37, t31, gm_srccfhjr4r9mdef, 476, 17);
            gm_b16_8 t38;
            GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t32 + t35 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
            gm_l3 = t38;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t40 = gm_l4;
                uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t42 = (t40 < t41);
                if (!t42) goto lb39;
                uint64_t t43 = gm_l4;
                uint64_t t44 = (uintptr_t)t43;
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srccfhjr4r9mdef, 478, 43);
                uint8_t t46 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t44 * 1));
                uint8_t t47 = gm_f_ident_char_pmpasvq(t46);
                if (!t47) goto lb39;
                {
                    uint64_t t50 = gm_l4;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    if (o52) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 478, 54);
                    uint64_t t53 = r51;
                    gm_l4 = t53;
                }
lc39:;
            }
lb39:;
        }
        {
            uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t55 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t56 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t57 = gm_l4;
            uint64_t t58 = (uintptr_t)t57;
            if (t56 > t58 || t58 > (uintptr_t)t54) gm_trap_slice(t56, t58, t54, gm_srccfhjr4r9mdef, 479, 16);
            gm_b16_8 t59;
            GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t55 + t56 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(t58 - t56));
            uint8_t t60 = 0;
            F.ret = t59;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        gm_b16_8 t62 = gm_l1;
        gm_b16_8 t63;
        GM_ST(void *, (uint8_t *)&t63, (void *)(gm_seccpktgmrevt));
        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(1));
        uint8_t t64 = gm_f_starts_with_6vvt01e(t62, t63);
        if (!t64) goto ie61;
        {
            gm_b16_8 t66;
            GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(0));
            uint8_t t67 = 0;
            F.ret = t66;
            goto gm_fret;
        }
        goto ix61;
ie61:;
ix61:;
    }
    {
        gm_b16_8 t68;
        GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(0));
        gm_l5 = t68;
    }
    {
        gm_l6 = ((int32_t)0LL);
    }
    {
        gm_l7 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t70 = gm_l7;
            uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t72 = (t70 < t71);
            if (!t72) goto lb69;
            {
                uint64_t t75 = gm_l7;
                uint64_t t76 = (uintptr_t)t75;
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_srccfhjr4r9mdef, 486, 14);
                uint8_t t78 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t76 * 1));
                gm_l8 = t78;
            }
            {
                uint8_t t80 = 0;
                uint8_t t81 = gm_l8;
                uint8_t t82 = ((t81 == ((uint8_t)34ULL)));
                if (!t82) {
                    uint8_t t83 = gm_l8;
                    uint8_t t84 = ((t83 == ((uint8_t)39ULL)));
                    t80 = t84;
                } else t80 = 1;
                if (!t80) goto ie79;
                {
                    gm_b16_8 t86 = gm_l1;
                    uint64_t t87 = gm_l7;
                    uint64_t t88 = gm_f_skip_literal_17lq2p8(t86, t87);
                    gm_l7 = t88;
                }
                {
                    goto lc69;
                }
                goto ix79;
ie79:;
ix79:;
            }
            {
                uint8_t t90 = gm_l8;
                gm_toh0ootodhjue t91;
                GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)40ULL));
                GM_ST(uint8_t, ((uint8_t *)&t91 + 1), ((uint8_t)91ULL));
                GM_ST(uint8_t, ((uint8_t *)&t91 + 2), ((uint8_t)123ULL));
                uint8_t t92 = 0;
                if (gm_eqjmccqdb90f(((uint8_t *)&t91) + 0, ((uint8_t *)&t90))) t92 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t91) + 1, ((uint8_t *)&t90))) t92 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t91) + 2, ((uint8_t *)&t90))) t92 = 1;
                uint8_t t93 = (uint8_t)(t92);
                if (!t93) goto ie89;
                {
                    int32_t t95 = gm_l6;
                    int32_t r96; bool o97 = __builtin_add_overflow(t95, ((int32_t)1LL), &r96);
                    if (o97) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 492, 13);
                    int32_t t98 = r96;
                    gm_l6 = t98;
                }
                goto ix89;
ie89:;
                uint8_t t100 = gm_l8;
                gm_toh0ootodhjue t101;
                GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)41ULL));
                GM_ST(uint8_t, ((uint8_t *)&t101 + 1), ((uint8_t)93ULL));
                GM_ST(uint8_t, ((uint8_t *)&t101 + 2), ((uint8_t)125ULL));
                uint8_t t102 = 0;
                if (gm_eqjmccqdb90f(((uint8_t *)&t101) + 0, ((uint8_t *)&t100))) t102 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t101) + 1, ((uint8_t *)&t100))) t102 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t101) + 2, ((uint8_t *)&t100))) t102 = 1;
                uint8_t t103 = (uint8_t)(t102);
                if (!t103) goto ie99;
                {
                    int32_t t105 = gm_l6;
                    int32_t r106; bool o107 = __builtin_sub_overflow(t105, ((int32_t)1LL), &r106);
                    if (o107) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 494, 13);
                    int32_t t108 = r106;
                    gm_l6 = t108;
                }
                goto ix99;
ie99:;
                uint8_t t110 = gm_l8;
                uint8_t t111 = ((t110 == ((uint8_t)61ULL)));
                if (!t111) goto ie109;
                int32_t t112 = gm_l6;
                uint8_t t113 = ((t112 == ((int32_t)0LL)));
                if (!t113) goto ie109;
                {
                    goto lb69;
                }
                goto ix109;
ie109:;
                uint8_t t116 = gm_l8;
                uint8_t t117 = gm_f_ident_char_pmpasvq(t116);
                if (!t117) goto ie115;
                {
                    uint64_t t119 = gm_l7;
                    gm_l9 = t119;
                }
                {
                    for (;;) {
                        uint64_t t121 = gm_l9;
                        uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        uint8_t t123 = (t121 < t122);
                        if (!t123) goto lb120;
                        uint64_t t124 = gm_l9;
                        uint64_t t125 = (uintptr_t)t124;
                        uint64_t t126 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t125 >= (uintptr_t)(t126)) gm_trap_index(t125, t126, gm_srccfhjr4r9mdef, 499, 47);
                        uint8_t t127 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t125 * 1));
                        uint8_t t128 = gm_f_ident_char_pmpasvq(t127);
                        if (!t128) goto lb120;
                        {
                            uint64_t t131 = gm_l9;
                            uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
                            if (o133) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 499, 58);
                            uint64_t t134 = r132;
                            gm_l9 = t134;
                        }
lc120:;
                    }
lb120:;
                }
                {
                    int32_t t136 = gm_l6;
                    uint8_t t137 = ((t136 == ((int32_t)0LL)));
                    if (!t137) goto ie135;
                    uint64_t t138 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    void * t139 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                    uint64_t t140 = gm_l7;
                    uint64_t t141 = (uintptr_t)t140;
                    uint64_t t142 = gm_l9;
                    uint64_t t143 = (uintptr_t)t142;
                    if (t141 > t143 || t143 > (uintptr_t)t138) gm_trap_slice(t141, t143, t138, gm_srccfhjr4r9mdef, 500, 31);
                    gm_b16_8 t144;
                    GM_ST(void *, (uint8_t *)&t144, (void *)(((uint8_t *)t139 + t141 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(t143 - t141));
                    gm_b16_8 t145;
                    GM_ST(void *, (uint8_t *)&t145, (void *)(gm_s7ipvt5onv6ht));
                    GM_ST(uintptr_t, (uint8_t *)&t145 + 8, (uintptr_t)(3));
                    uint8_t t146 = gm_f_starts_with_6vvt01e(t144, t145);
                    if (!t146) goto ie135;
                    {
                        uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        void * t149 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                        uint64_t t150 = gm_l7;
                        uint64_t t151 = (uintptr_t)t150;
                        uint64_t t152 = gm_l9;
                        uint64_t t153 = (uintptr_t)t152;
                        if (t151 > t153 || t153 > (uintptr_t)t148) gm_trap_slice(t151, t153, t148, gm_srccfhjr4r9mdef, 500, 70);
                        gm_b16_8 t154;
                        GM_ST(void *, (uint8_t *)&t154, (void *)(((uint8_t *)t149 + t151 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(t153 - t151));
                        gm_l5 = t154;
                    }
                    goto ix135;
ie135:;
ix135:;
                }
                {
                    uint64_t t155 = gm_l9;
                    gm_l7 = t155;
                }
                {
                    goto lc69;
                }
                goto ix115;
ie115:;
ix115:;
ix109:;
ix99:;
ix89:;
            }
            {
                uint64_t t156 = gm_l7;
                uint64_t r157; bool o158 = __builtin_add_overflow(t156, ((uint64_t)1ULL), &r157);
                if (o158) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 504, 9);
                uint64_t t159 = r157;
                gm_l7 = t159;
            }
lc69:;
        }
lb69:;
    }
    {
        gm_b16_8 t160 = gm_l5;
        uint8_t t161 = 0;
        F.ret = t160;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_decls_of_srep9du(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_decls_of_srep9du F;
    void * gm_l0; /* out */
    gm_b16_8 gm_l1; /* text */
    uint8_t gm_l2; /* rank */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* item */
    GM_STACK_CHECK(&F, 317u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_b16_8 t4 = gm_f_decl_items_l330ksi((void *)NULL, cx, NULL, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
                    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 16));
                    gm_l5 = t16;
                    {
                        void * t18 = gm_l0;
                        gm_t84tkl8vf34ko t19;
                        memset((uint8_t *)&t19 + 49, 0, 3);
                        gm_b16_8 t20 = gm_l5;
                        GM_ST(gm_b16_8, (uint8_t *)&t19 + 0, t20);
                        gm_b16_8 t21 = gm_l5;
                        gm_b16_8 t22 = gm_f_declared_name_hovvm2b(t21);
                        GM_ST(gm_b16_8, (uint8_t *)&t19 + 16, t22);
                        gm_b16_8 t23 = gm_l5;
                        gm_b16_8 t24 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t23);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t19 + 32, t24);
                        uint8_t t25 = gm_l2;
                        GM_ST(uint8_t, (uint8_t *)&t19 + 48, t25);
                        GM_ST(uint32_t, (uint8_t *)&t19 + 52, ((uint32_t)0ULL));
                        gm_tftb1odc739t5 t26 = gm_f_push_rda4o9n(t18, &t19);
                        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srccfhjr4r9mdef, 376, 9);
                        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 375, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l4;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l4 = t32;
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
inline __attribute__((aligned(4))) void gm_f_external_f65nm8q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_external_f65nm8q F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* r */
    gm_b16_8 gm_l2; /* rest */
    gm_b16_8 gm_l3; /* r */
    gm_b16_8 gm_l4; /* r */
    GM_STACK_CHECK(&F, 577u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, gm_csu9mkbsfpo);
        gm_th2lqbo77pvsc t5;
        gm_f_strip_prefix_b4949s8((void *)&t5, t3, t4);
        if (!((((uint8_t *)&t5))[16] != 0)) goto ie2;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_l1 = t6;
        {
            gm_b16_8 t8 = gm_l1;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t8);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(0));
        gm_l2 = t9;
    }
    {
        gm_b16_8 t11 = gm_l0;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_soa9mnl4ahjud));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(7));
        gm_th2lqbo77pvsc t13;
        gm_f_strip_prefix_b4949s8((void *)&t13, t11, t12);
        if (!((((uint8_t *)&t13))[16] != 0)) goto ie10;
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        gm_l3 = t14;
        {
            gm_b16_8 t16 = gm_l3;
            gm_l2 = t16;
        }
        goto ix10;
ie10:;
        {
            gm_b16_8 t18 = gm_l0;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1oiqt105ulvb));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(19));
            gm_th2lqbo77pvsc t20;
            gm_f_strip_prefix_b4949s8((void *)&t20, t18, t19);
            gm_b16_8 t21;
            if (((((uint8_t *)&t20))[16] != 0)) {
                gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 0));
                t21 = t22;
            } else {
                gm_th2lqbo77pvsc t23;
                memset(&t23, 0, sizeof t23);
                uint8_t t24 = 0;
                F.ret = t23;
                goto gm_fret;
            }
            gm_l4 = t21;
        }
        {
            gm_b16_8 t25 = gm_l4;
            gm_tnpifv20irnph t26;
            GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
            gm_tabb710d9mq2e t27;
            gm_f_format_69nn7l3((void *)&t27, (void *)NULL, cx, NULL, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_srccfhjr4r9mdef, 242, 16);
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
            gm_l2 = t29;
        }
ix10:;
    }
    {
        gm_b16_8 t30 = gm_l2;
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_so0h0lkl6bp4l));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(38));
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(0));
        gm_tabb710d9mq2e t33;
        gm_f_replace_alloc_t7hndlq((void *)&t33, (void *)NULL, cx, NULL, t30, t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 8, gm_srccfhjr4r9mdef, 244, 12);
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 8));
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t35);
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_575gb0c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_idfg0st(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_69vpqmq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_69vpqmq F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* d */
    uint64_t gm_l4; /* k */
    gm_b16_8 gm_l5; /* d */
    GM_STACK_CHECK(&F, 1246u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie5;
        {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_b16_8 t12 = gm_l1;
        gm_th2lqbo77pvsc t13;
        gm_f_c_fn_name_77uspqp((void *)&t13, t12);
        if (!((((uint8_t *)&t13))[16] != 0)) goto ie11;
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        gm_l2 = t14;
        void * t15 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l2);
        gm_b16_8 t16 = gm_l2;
        gm_tf5srh4mp4b3i t17 = gm_f_get_espvf96(t15, t16);
        uint8_t t18;
        if (((((uint8_t *)&t17))[8] != 0)) goto mn20;
        t18 = ((uint8_t)0);
        goto me19;
mn20:;
        t18 = ((uint8_t)1);
        goto me19;
mn21:;
        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 557, 40);
me19:;
        if (!t18) goto ie11;
        {
            gm_b16_8 t23 = gm_l1;
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_soa9mnl4ahjud));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(7));
            gm_th2lqbo77pvsc t25;
            gm_f_strip_prefix_b4949s8((void *)&t25, t23, t24);
            gm_b16_8 t26;
            if (((((uint8_t *)&t25))[16] != 0)) {
                gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 0));
                t26 = t27;
            } else {
                gm_b16_8 t28 = gm_l1;
                t26 = t28;
            }
            gm_l3 = t26;
        }
        {
            gm_b16_8 t30 = gm_l3;
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sd1uj6gf5v8qh));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(7));
            uint8_t t32 = gm_f_contains_gkki4kq(t30, t31);
            uint8_t t33 = (uint8_t)!t32;
            if (!t33) goto ie29;
            {
                gm_b16_8 t35 = gm_l3;
                gm_tnpifv20irnph t36;
                GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t35);
                gm_tabb710d9mq2e t37;
                gm_f_format_rse2efa((void *)&t37, (void *)NULL, cx, NULL, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_srccfhjr4r9mdef, 559, 53);
                gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
                gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                gm_l3 = t39;
            }
            goto ix29;
ie29:;
ix29:;
        }
        {
            void * t40 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l1);
            gm_t84tkl8vf34ko t41;
            memset((uint8_t *)&t41 + 49, 0, 3);
            gm_b16_8 t42 = gm_l3;
            gm_tnpifv20irnph t43;
            GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t42);
            gm_tabb710d9mq2e t44;
            gm_f_format_uaq30dv((void *)&t44, (void *)NULL, cx, NULL, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_srccfhjr4r9mdef, 560, 35);
            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
            GM_ST(gm_b16_8, (uint8_t *)&t41 + 0, t46);
            gm_b16_8 t47 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t41 + 16, t47);
            gm_b16_8 t48 = gm_l3;
            gm_b16_8 t49 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t41 + 32, t49);
            uint8_t t50 = GM_LD(uint8_t, gm_c72j1g8d455);
            GM_ST(uint8_t, (uint8_t *)&t41 + 48, t50);
            GM_ST(uint32_t, (uint8_t *)&t41 + 52, ((uint32_t)0ULL));
            gm_tftb1odc739t5 t51 = gm_f_push_rda4o9n(t40, &t41);
            if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srccfhjr4r9mdef, 560, 13);
            gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
        }
        {
            void * t53 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l2);
            gm_b16_8 t54 = gm_l2;
            gm_tf5srh4mp4b3i t55 = gm_f_get_espvf96(t53, t54);
            uint64_t t56;
            if (((((uint8_t *)&t55))[8] != 0)) {
                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&t55) + 0));
                t56 = t57;
            } else {
                gm_trap("reached `@unreachable()`", gm_srccfhjr4r9mdef, 561, 46);
            }
            gm_l4 = t56;
        }
        {
            uint64_t t58 = gm_l4;
            uint64_t t59 = (uintptr_t)t58;
            uint64_t t60 = GM_LD(uintptr_t, (((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l15) + 0) + 8);
            if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srccfhjr4r9mdef, 562, 13);
            void * t61 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l15) + 0)) + t59 * 40);
            gm_t6j6smj7qoctu t62;
            gm_b16_8 t63 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 0, t63);
            gm_b16_8 t64 = gm_l1;
            gm_th2lqbo77pvsc t65;
            gm_f_external_f65nm8q((void *)&t65, (void *)NULL, cx, NULL, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t66;
            if (((((uint8_t *)&t65))[16] != 0)) {
                gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 0));
                t66 = t67;
            } else {
                gm_b16_8 t68 = gm_l1;
                t66 = t68;
            }
            gm_tnpifv20irnph t69;
            GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t66);
            gm_tabb710d9mq2e t70;
            gm_f_format_u8fgvjp((void *)&t70, (void *)NULL, cx, NULL, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 8, gm_srccfhjr4r9mdef, 562, 53);
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 16, t72);
            gm_tftb1odc739t5 t73 = gm_f_push_0ek3ea4(t61, &t62);
            if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_srccfhjr4r9mdef, 562, 13);
            gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
        }
        {
            gm_b0_1 t75;
            uint8_t t76 = 0;
            F.ret = t75;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b16_8 t77 = gm_l1;
        gm_th2lqbo77pvsc t78;
        gm_f_external_f65nm8q((void *)&t78, (void *)NULL, cx, NULL, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t79;
        if (((((uint8_t *)&t78))[16] != 0)) {
            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&t78) + 0));
            t79 = t80;
        } else {
            gm_b16_8 t81 = gm_l1;
            t79 = t81;
        }
        gm_l5 = t79;
    }
    {
        void * t82 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l1);
        gm_t84tkl8vf34ko t83;
        memset((uint8_t *)&t83 + 49, 0, 3);
        gm_b16_8 t84 = gm_l5;
        gm_tnpifv20irnph t85;
        GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t84);
        gm_tabb710d9mq2e t86;
        gm_f_format_uaq30dv((void *)&t86, (void *)NULL, cx, NULL, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 8, gm_srccfhjr4r9mdef, 566, 31);
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
        gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
        GM_ST(gm_b16_8, (uint8_t *)&t83 + 0, t88);
        gm_b16_8 t89 = gm_l5;
        gm_b16_8 t90 = gm_f_declared_name_hovvm2b(t89);
        GM_ST(gm_b16_8, (uint8_t *)&t83 + 16, t90);
        gm_b16_8 t91 = gm_l5;
        gm_b16_8 t92 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t91);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t83 + 32, t92);
        uint8_t t93 = GM_LD(uint8_t, gm_c72j1g8d455);
        GM_ST(uint8_t, (uint8_t *)&t83 + 48, t93);
        GM_ST(uint32_t, (uint8_t *)&t83 + 52, ((uint32_t)0ULL));
        gm_tftb1odc739t5 t94 = gm_f_push_rda4o9n(t82, &t83);
        if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_srccfhjr4r9mdef, 566, 9);
        gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 555, 5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fe8qj55(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_fe8qj55 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    uint64_t gm_l2; /* end */
    gm_b16_8 gm_l3; /* t */
    GM_STACK_CHECK(&F, 480u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l4));
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r7; bool o8 = __builtin_add_overflow(t5, t6, &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 388, 16);
        uint64_t t9 = r7;
        gm_l2 = t9;
    }
    {
        uint64_t t11 = gm_l2;
        uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0) + 8);
        uint8_t t13 = (t11 < t12);
        if (!t13) goto ie10;
        {
            uint64_t t15 = gm_l2;
            uint64_t r16; bool o17 = __builtin_add_overflow(t15, ((uint64_t)1ULL), &r16);
            if (o17) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 389, 29);
            uint64_t t18 = r16;
            gm_l2 = t18;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        int32_t t20 = GM_LD(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3));
        uint8_t t21 = ((t20 == ((int32_t)0LL)));
        if (!t21) goto ie19;
        gm_b16_8 t22 = gm_l1;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_seccpktgmrevt));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
        uint8_t t24 = gm_f_starts_with_6vvt01e(t22, t23);
        if (!t24) goto ie19;
        {
            void * t26 = (void *)((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l1);
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0) + 8);
            void * t28 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0)));
            uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l2));
            uint64_t t30 = (uintptr_t)t29;
            uint64_t t31 = gm_l2;
            uint64_t t32 = (uintptr_t)t31;
            if (t30 > t32 || t32 > (uintptr_t)t27) gm_trap_slice(t30, t32, t27, gm_srccfhjr4r9mdef, 391, 24);
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t28 + t30 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t30));
            gm_tftb1odc739t5 t34 = gm_f_push_3ef746b(t26, t33);
            if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srccfhjr4r9mdef, 391, 13);
            gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
        }
        {
            uint64_t t36 = gm_l2;
            GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l2), t36);
        }
        {
            uint64_t t37 = gm_l2;
            GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l4), t37);
        }
        {
            gm_b0_1 t38;
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        int32_t t40 = GM_LD(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3));
        gm_b16_8 t41 = gm_l1;
        int32_t t42 = gm_f_brace_balance_aofv41t(t41);
        int32_t r43; bool o44 = __builtin_add_overflow(t40, t42, &r43);
        if (o44) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 396, 9);
        int32_t t45 = r43;
        GM_ST(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3), t45);
    }
    {
        gm_b16_8 t46 = gm_l1;
        gm_b16_8 t47 = gm_f_trim_end_6sjv6rg(t46);
        gm_l3 = t47;
    }
    {
        int32_t t49 = GM_LD(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3));
        uint8_t t50 = (t49 <= ((int32_t)0LL));
        if (!t50) goto ie48;
        uint8_t t51 = 0;
        gm_b16_8 t52 = gm_l3;
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_spod3tmkh25up));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(1));
        uint8_t t54 = gm_f_ends_with_biriu5l(t52, t53);
        if (!t54) {
            gm_b16_8 t55 = gm_l3;
            gm_b16_8 t56;
            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(1));
            uint8_t t57 = gm_f_ends_with_biriu5l(t55, t56);
            t51 = t57;
        } else t51 = 1;
        if (!t51) goto ie48;
        {
            void * t59 = (void *)((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l1);
            uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0) + 8);
            void * t61 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l0)));
            uint64_t t62 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l2));
            uint64_t t63 = (uintptr_t)t62;
            uint64_t t64 = gm_l2;
            uint64_t t65 = (uintptr_t)t64;
            if (t63 > t65 || t65 > (uintptr_t)t60) gm_trap_slice(t63, t65, t60, gm_srccfhjr4r9mdef, 399, 24);
            gm_b16_8 t66;
            GM_ST(void *, (uint8_t *)&t66, (void *)(((uint8_t *)t61 + t63 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(t65 - t63));
            gm_tftb1odc739t5 t67 = gm_f_push_3ef746b(t59, t66);
            if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 1, gm_srccfhjr4r9mdef, 399, 13);
            gm_b0_1 t68 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 1));
        }
        {
            uint64_t t69 = gm_l2;
            GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l2), t69);
        }
        {
            GM_ST(int32_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l3), ((int32_t)0LL));
        }
        goto ix48;
ie48:;
ix48:;
    }
    {
        uint64_t t70 = gm_l2;
        GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_decl_items_l330ksi *)((&F.h)->up))->l4), t70);
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 387, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_j11vulv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_j11vulv F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* d */
    GM_STACK_CHECK(&F, 647u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t6 = gm_l1;
        gm_th2lqbo77pvsc t7;
        gm_f_c_fn_name_77uspqp((void *)&t7, t6);
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie5;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l2 = t8;
        void * t9 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l3);
        gm_b16_8 t10 = gm_l2;
        gm_ttsm01t0d8srg t11 = gm_f_get_ece4opn(t9, t10);
        uint8_t t12;
        if (((((uint8_t *)&t11))[0] != 0)) goto mn14;
        t12 = ((uint8_t)0);
        goto me13;
mn14:;
        t12 = ((uint8_t)1);
        goto me13;
mn15:;
        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 118, 40);
me13:;
        if (!t12) goto ie5;
        {
            gm_b16_8 t17 = gm_l1;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_soa9mnl4ahjud));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(7));
            gm_th2lqbo77pvsc t19;
            gm_f_strip_prefix_b4949s8((void *)&t19, t17, t18);
            gm_b16_8 t20;
            if (((((uint8_t *)&t19))[16] != 0)) {
                gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 0));
                t20 = t21;
            } else {
                gm_b16_8 t22 = gm_l1;
                t20 = t22;
            }
            gm_l3 = t20;
        }
        {
            gm_b16_8 t24 = gm_l3;
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sd1uj6gf5v8qh));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(7));
            uint8_t t26 = gm_f_contains_gkki4kq(t24, t25);
            uint8_t t27 = (uint8_t)!t26;
            if (!t27) goto ie23;
            {
                gm_b16_8 t29 = gm_l3;
                gm_tnpifv20irnph t30;
                GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t29);
                gm_tabb710d9mq2e t31;
                gm_f_format_rse2efa((void *)&t31, (void *)NULL, cx, NULL, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_srccfhjr4r9mdef, 120, 53);
                gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
                gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                gm_l3 = t33;
            }
            goto ix23;
ie23:;
ix23:;
        }
        {
            void * t34 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
            gm_b16_8 t35 = gm_l3;
            gm_tftb1odc739t5 t36 = gm_f_extend_0cs85gs(t34, t35);
            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srccfhjr4r9mdef, 121, 13);
            gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
        }
        {
            void * t38 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
            gm_tftb1odc739t5 t39 = gm_f_push_t83rg2m(t38, ((uint8_t)10ULL));
            if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srccfhjr4r9mdef, 122, 13);
            gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
        }
        {
            void * t41 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l11);
            gm_b16_8 t42 = gm_l1;
            gm_th2lqbo77pvsc t43;
            gm_f_external_f65nm8q((void *)&t43, (void *)NULL, cx, NULL, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t44;
            if (((((uint8_t *)&t43))[16] != 0)) {
                gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 0));
                t44 = t45;
            } else {
                gm_b16_8 t46 = gm_l1;
                t44 = t46;
            }
            gm_tnpifv20irnph t47;
            GM_ST(gm_b16_8, ((uint8_t *)&t47 + 0), t44);
            gm_tftb1odc739t5 t48 = gm_f_write_d8c7tpm(t41, t47);
            if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srccfhjr4r9mdef, 123, 13);
            gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
        }
        {
            gm_b0_1 t50;
            uint8_t t51 = 0;
            F.ret = t50;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t52 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
        gm_b16_8 t53 = gm_l1;
        gm_th2lqbo77pvsc t54;
        gm_f_external_f65nm8q((void *)&t54, (void *)NULL, cx, NULL, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t55;
        if (((((uint8_t *)&t54))[16] != 0)) {
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 0));
            t55 = t56;
        } else {
            gm_b16_8 t57 = gm_l1;
            t55 = t57;
        }
        gm_tftb1odc739t5 t58 = gm_f_extend_0cs85gs(t52, t55);
        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_srccfhjr4r9mdef, 126, 9);
        gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
    }
    {
        void * t60 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
        gm_tftb1odc739t5 t61 = gm_f_push_t83rg2m(t60, ((uint8_t)10ULL));
        if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_srccfhjr4r9mdef, 127, 9);
        gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 117, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_keqa9hq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_keqa9hq F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t6 = gm_l1;
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_seccpktgmrevt));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
        uint8_t t8 = gm_f_starts_with_6vvt01e(t6, t7);
        uint8_t t9 = (uint8_t)!t8;
        if (!t9) goto ie5;
        {
            uint64_t t11 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_code_bytes_u2ir6qv *)((&F.h)->up))->l1));
            uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r13; bool o14 = __builtin_add_overflow(t12, ((uint64_t)1ULL), &r13);
            if (o14) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 208, 45);
            uint64_t t15 = r13;
            uint64_t r16; bool o17 = __builtin_add_overflow(t11, t15, &r16);
            if (o17) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 208, 40);
            uint64_t t18 = r16;
            GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_code_bytes_u2ir6qv *)((&F.h)->up))->l1), t18);
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 207, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_n9765ef(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_n9765ef F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* def */
    gm_t6j6smj7qoctu gm_l3; /* eq */
    gm_b16_8 gm_l4; /* head */
    gm_b16_8 gm_l5; /* d */
    gm_b16_8 gm_l6; /* name */
    GM_STACK_CHECK(&F, 1306u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie5;
        {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        uint8_t t12 = 0;
        uint8_t t13 = 0;
        gm_b16_8 t14 = gm_l1;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_seccpktgmrevt));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(1));
        uint8_t t16 = gm_f_starts_with_6vvt01e(t14, t15);
        if (!t16) {
            gm_b16_8 t17 = gm_l1;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sasea0sobfkrs));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(7));
            uint8_t t19 = gm_f_starts_with_6vvt01e(t17, t18);
            t13 = t19;
        } else t13 = 1;
        if (!t13) {
            gm_b16_8 t20 = gm_l1;
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_shv3ttlq9l359));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(14));
            uint8_t t22 = gm_f_starts_with_6vvt01e(t20, t21);
            t12 = t22;
        } else t12 = 1;
        if (!t12) goto ie11;
        {
            void * t24 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l1);
            gm_t84tkl8vf34ko t25;
            memset((uint8_t *)&t25 + 49, 0, 3);
            gm_b16_8 t26 = gm_l1;
            gm_tnpifv20irnph t27;
            GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t26);
            gm_tabb710d9mq2e t28;
            gm_f_format_uaq30dv((void *)&t28, (void *)NULL, cx, NULL, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_srccfhjr4r9mdef, 575, 35);
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t30);
            gm_b16_8 t31 = gm_l1;
            gm_b16_8 t32 = gm_f_declared_name_hovvm2b(t31);
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 16, t32);
            gm_b16_8 t33 = gm_l1;
            gm_b16_8 t34 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 32, t34);
            uint8_t t35 = GM_LD(uint8_t, gm_c4crumb5mc9);
            GM_ST(uint8_t, (uint8_t *)&t25 + 48, t35);
            GM_ST(uint32_t, (uint8_t *)&t25 + 52, ((uint32_t)0ULL));
            gm_tftb1odc739t5 t36 = gm_f_push_rda4o9n(t24, &t25);
            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srccfhjr4r9mdef, 575, 13);
            gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
        }
        {
            gm_b0_1 t38;
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b16_8 t40 = gm_l1;
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_soa9mnl4ahjud));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(7));
        gm_th2lqbo77pvsc t42;
        gm_f_strip_prefix_b4949s8((void *)&t42, t40, t41);
        gm_b16_8 t43;
        if (((((uint8_t *)&t42))[16] != 0)) {
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 0));
            t43 = t44;
        } else {
            gm_ttu09lij36f8c t45;
            memset(&t45, 0, sizeof t45);
            uint8_t t46 = 0;
            ((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->ret = t45;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t46;
            goto gm_lad;
        }
        gm_l2 = t43;
    }
    {
        gm_b16_8 t47;
        gm_b16_8 t49 = gm_l2;
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_sao4mumvadoct));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(3));
        gm_ttu09lij36f8c t51;
        gm_f_split_once_mqcu1ce((void *)&t51, t49, t50);
        if (!((((uint8_t *)&t51))[32] != 0)) goto ie48;
        gm_l3 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t51) + 0));
        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
        gm_b16_8 t54 = t53;
        t47 = t54;
        goto ix48;
ie48:;
        gm_b16_8 t56 = gm_l2;
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_spod3tmkh25up));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(1));
        gm_th2lqbo77pvsc t58;
        gm_f_strip_suffix_8155gbi((void *)&t58, t56, t57);
        gm_b16_8 t59;
        if (((((uint8_t *)&t58))[16] != 0)) {
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 0));
            t59 = t60;
        } else {
            gm_ttu09lij36f8c t61;
            memset(&t61, 0, sizeof t61);
            uint8_t t62 = 0;
            ((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->ret = t61;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t62;
            goto gm_lad;
        }
        gm_b16_8 t63 = t59;
        t47 = t63;
ix48:;
        gm_l4 = t47;
    }
    {
        gm_b16_8 t64 = gm_l4;
        gm_tnpifv20irnph t65;
        GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t64);
        gm_tabb710d9mq2e t66;
        gm_f_format_8l8mb6d((void *)&t66, (void *)NULL, cx, NULL, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_srccfhjr4r9mdef, 580, 14);
        gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
        gm_l5 = t67;
    }
    {
        gm_b16_8 t68 = gm_l5;
        gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
        gm_b16_8 t70 = gm_f_declared_name_hovvm2b(t69);
        gm_l6 = t70;
    }
    {
        void * t71 = (void *)((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l1);
        gm_t84tkl8vf34ko t72;
        memset((uint8_t *)&t72 + 49, 0, 3);
        gm_b16_8 t73 = gm_l5;
        gm_tnpifv20irnph t74;
        GM_ST(gm_b16_8, ((uint8_t *)&t74 + 0), t73);
        gm_tabb710d9mq2e t75;
        gm_f_format_1u4jnnu((void *)&t75, (void *)NULL, cx, NULL, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_srccfhjr4r9mdef, 582, 31);
        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
        gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
        GM_ST(gm_b16_8, (uint8_t *)&t72 + 0, t77);
        gm_b16_8 t78 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t72 + 16, t78);
        gm_b16_8 t79 = gm_l5;
        gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
        gm_b16_8 t81 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t72 + 32, t81);
        uint8_t t82 = GM_LD(uint8_t, gm_c4crumb5mc9);
        GM_ST(uint8_t, (uint8_t *)&t72 + 48, t82);
        GM_ST(uint32_t, (uint8_t *)&t72 + 52, ((uint32_t)0ULL));
        gm_tftb1odc739t5 t83 = gm_f_push_rda4o9n(t71, &t72);
        if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_srccfhjr4r9mdef, 582, 9);
        gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
    }
    {
        gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&((struct gm_fr_split_by_file_hgjqgsp *)((&F.h)->up))->l19) + 0));
        gm_b16_8 t86 = t85;
        gm_b16_8 t87 = gm_l6;
        uint64_t t88 = gm_f_hash64_apkb7bo(t87);
        if (((uint64_t)16ULL) == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 583, 23);
        uint64_t t89 = (uint64_t)(t88 % ((uint64_t)16ULL));
        uint64_t t90 = (uint64_t)t89;
        uint64_t t91 = (uintptr_t)t90;
        uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&t86) + 8);
        if (t91 >= (uintptr_t)(t92)) gm_trap_index(t91, t92, gm_srccfhjr4r9mdef, 583, 9);
        void * t93 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t86)) + t91 * 40);
        gm_b16_8 t94 = gm_l2;
        gm_tftb1odc739t5 t95 = gm_f_push_3ef746b(t93, t94);
        if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_srccfhjr4r9mdef, 583, 9);
        gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 572, 5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ui0bpsf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_ui0bpsf F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l2; /* def */
    gm_t6j6smj7qoctu gm_l3; /* eq */
    gm_b16_8 gm_l4; /* head */
    GM_STACK_CHECK(&F, 732u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint8_t t6 = 0;
        uint8_t t7 = 0;
        uint8_t t8 = 0;
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
        if (!t10) {
            gm_b16_8 t11 = gm_l1;
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_seccpktgmrevt));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
            uint8_t t13 = gm_f_starts_with_6vvt01e(t11, t12);
            t8 = t13;
        } else t8 = 1;
        if (!t8) {
            gm_b16_8 t14 = gm_l1;
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sasea0sobfkrs));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(7));
            uint8_t t16 = gm_f_starts_with_6vvt01e(t14, t15);
            t7 = t16;
        } else t7 = 1;
        if (!t7) {
            gm_b16_8 t17 = gm_l1;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_shv3ttlq9l359));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(14));
            uint8_t t19 = gm_f_starts_with_6vvt01e(t17, t18);
            t6 = t19;
        } else t6 = 1;
        if (!t6) goto ie5;
        {
            void * t21 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
            gm_b16_8 t22 = gm_l1;
            gm_tftb1odc739t5 t23 = gm_f_extend_0cs85gs(t21, t22);
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srccfhjr4r9mdef, 131, 13);
            gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
        }
        {
            void * t25 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
            gm_tftb1odc739t5 t26 = gm_f_push_t83rg2m(t25, ((uint8_t)10ULL));
            if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srccfhjr4r9mdef, 132, 13);
            gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
        }
        {
            gm_b0_1 t28;
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_b16_8 t30 = gm_l1;
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_soa9mnl4ahjud));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(7));
        gm_th2lqbo77pvsc t32;
        gm_f_strip_prefix_b4949s8((void *)&t32, t30, t31);
        gm_b16_8 t33;
        if (((((uint8_t *)&t32))[16] != 0)) {
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 0));
            t33 = t34;
        } else {
            gm_ttu09lij36f8c t35;
            memset(&t35, 0, sizeof t35);
            uint8_t t36 = 0;
            ((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->ret = t35;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t36;
            goto gm_lad;
        }
        gm_l2 = t33;
    }
    {
        gm_b16_8 t37;
        gm_b16_8 t39 = gm_l2;
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sao4mumvadoct));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(3));
        gm_ttu09lij36f8c t41;
        gm_f_split_once_mqcu1ce((void *)&t41, t39, t40);
        if (!((((uint8_t *)&t41))[32] != 0)) goto ie38;
        gm_l3 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t41) + 0));
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
        gm_b16_8 t44 = t43;
        t37 = t44;
        goto ix38;
ie38:;
        gm_b16_8 t46 = gm_l2;
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_spod3tmkh25up));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(1));
        gm_th2lqbo77pvsc t48;
        gm_f_strip_suffix_8155gbi((void *)&t48, t46, t47);
        gm_b16_8 t49;
        if (((((uint8_t *)&t48))[16] != 0)) {
            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&t48) + 0));
            t49 = t50;
        } else {
            gm_ttu09lij36f8c t51;
            memset(&t51, 0, sizeof t51);
            uint8_t t52 = 0;
            ((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->ret = t51;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t52;
            goto gm_lad;
        }
        gm_b16_8 t53 = t49;
        t37 = t53;
ix38:;
        gm_l4 = t37;
    }
    {
        void * t54 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l9);
        gm_b16_8 t55 = gm_l4;
        gm_tnpifv20irnph t56;
        GM_ST(gm_b16_8, ((uint8_t *)&t56 + 0), t55);
        gm_tftb1odc739t5 t57 = gm_f_write_815rgbg(t54, t56);
        if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srccfhjr4r9mdef, 137, 9);
        gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
    }
    {
        void * t59 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l10);
        gm_b16_8 t60 = gm_l2;
        gm_tftb1odc739t5 t61 = gm_f_extend_0cs85gs(t59, t60);
        if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_srccfhjr4r9mdef, 138, 9);
        gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
    }
    {
        void * t63 = (void *)((uint8_t *)&((struct gm_fr_split_shared_a91530r *)((&F.h)->up))->l10);
        gm_tftb1odc739t5 t64 = gm_f_push_t83rg2m(t63, ((uint8_t)10ULL));
        if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_srccfhjr4r9mdef, 139, 9);
        gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 129, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vn00ddp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_vn00ddp F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_b16_8 t6 = gm_l1;
        gm_b16_8 t7 = gm_f_trim_fdrfe6i(t6);
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&t7) + 8);
        uint8_t t9 = (t8 > ((uint64_t)0ULL));
        if (!t9) goto ie5;
        gm_b16_8 t10 = gm_l1;
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_seccpktgmrevt));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(1));
        uint8_t t12 = gm_f_starts_with_6vvt01e(t10, t11);
        uint8_t t13 = (uint8_t)!t12;
        if (!t13) goto ie5;
        {
            gm_th2lqbo77pvsc t15;
            memset(&t15, 0, sizeof t15);
            uint8_t t16 = 0;
            ((struct gm_fr_items_of_q103oiv *)((&F.h)->up))->ret = t15;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t16;
            goto gm_lad;
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 351, 5);
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
__attribute__((aligned(4))) void gm_f_function_o2lv8g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_function_o2lv8g6 F;
    gm_b16_8 gm_l0; /* item */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* end */
    gm_b16_8 gm_l3; /* pre */
    gm_b16_8 gm_l4; /* text */
    uint64_t gm_l5; /* first_end */
    gm_b16_8 gm_l6; /* first */
    gm_b16_8 gm_l7; /* def */
    gm_b16_8 gm_l8; /* ext */
    gm_b16_8 gm_l9; /* body */
    int32_t gm_l10; /* depth */
    gm_tf5srh4mp4b3i gm_l11; /* open */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint8_t gm_l15; /* c */
    uint64_t gm_l16; /* at */
    gm_b16_8 gm_l17; /* header */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint8_t gm_l20; /* c */
    GM_STACK_CHECK(&F, 1969u);
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
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srccfhjr4r9mdef, 251, 32);
            uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t7 * 1));
            uint8_t t10 = ((t9 == ((uint8_t)35ULL)));
            if (!t10) goto lb2;
            {
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t14 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t15 = gm_l1;
                uint64_t t16 = (uintptr_t)t15;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint64_t t18 = (uintptr_t)t17;
                if (t16 > t18 || t18 > (uintptr_t)t13) gm_trap_slice(t16, t18, t13, gm_srccfhjr4r9mdef, 252, 25);
                gm_b16_8 t19;
                GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t14 + t16 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(t18 - t16));
                gm_b16_8 t20;
                GM_ST(void *, (uint8_t *)&t20, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
                gm_tf5srh4mp4b3i t21 = gm_f_find_nab29co(t19, t20);
                uint64_t t22;
                if (((((uint8_t *)&t21))[8] != 0)) {
                    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&t21) + 0));
                    t22 = t23;
                } else {
                    goto lb2;
                }
                gm_l2 = t22;
            }
            {
                uint64_t t24 = gm_l1;
                uint64_t t25 = gm_l2;
                uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                if (o27) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 253, 18);
                uint64_t t28 = r26;
                uint64_t r29; bool o30 = __builtin_add_overflow(t24, t28, &r29);
                if (o30) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 253, 9);
                uint64_t t31 = r29;
                gm_l1 = t31;
            }
lc2:;
        }
lb2:;
    }
    {
        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t33 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t34 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t35 = gm_l1;
        uint64_t t36 = (uintptr_t)t35;
        if (t34 > t36 || t36 > (uintptr_t)t32) gm_trap_slice(t34, t36, t32, gm_srccfhjr4r9mdef, 255, 12);
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(((uint8_t *)t33 + t34 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(t36 - t34));
        gm_l3 = t37;
    }
    {
        uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t39 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t40 = gm_l1;
        uint64_t t41 = (uintptr_t)t40;
        uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t43 = (uintptr_t)t42;
        if (t41 > t43 || t43 > (uintptr_t)t38) gm_trap_slice(t41, t43, t38, gm_srccfhjr4r9mdef, 256, 13);
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(((uint8_t *)t39 + t41 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(t43 - t41));
        gm_l4 = t44;
    }
    {
        gm_b16_8 t45 = gm_l4;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t47 = gm_f_find_nab29co(t45, t46);
        uint64_t t48;
        if (((((uint8_t *)&t47))[8] != 0)) {
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&t47) + 0));
            t48 = t49;
        } else {
            uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            t48 = t50;
        }
        gm_l5 = t48;
    }
    {
        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        void * t52 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
        uint64_t t53 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t54 = gm_l5;
        uint64_t t55 = (uintptr_t)t54;
        if (t53 > t55 || t55 > (uintptr_t)t51) gm_trap_slice(t53, t55, t51, gm_srccfhjr4r9mdef, 258, 14);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t52 + t53 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t53));
        gm_l6 = t56;
    }
    {
        gm_b16_8 t57 = gm_l0;
        gm_l7 = t57;
    }
    {
        gm_b16_8 t59 = gm_l6;
        gm_th2lqbo77pvsc t60;
        gm_f_external_f65nm8q((void *)&t60, (void *)NULL, cx, NULL, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t60))[16] != 0)) goto ie58;
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
        gm_l8 = t61;
        {
            gm_tfeab279c16rr t63;
            gm_b16_8 t64 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t63 + 0, t64);
            gm_b16_8 t65 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t63 + 16, t65);
            uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            void * t67 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
            uint64_t t68 = gm_l5;
            uint64_t t69 = (uintptr_t)t68;
            uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint64_t t71 = (uintptr_t)t70;
            if (t69 > t71 || t71 > (uintptr_t)t66) gm_trap_slice(t69, t71, t66, gm_srccfhjr4r9mdef, 261, 46);
            gm_b16_8 t72;
            GM_ST(void *, (uint8_t *)&t72, (void *)(((uint8_t *)t67 + t69 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(t71 - t69));
            GM_ST(gm_b16_8, (uint8_t *)&t63 + 32, t72);
            gm_tabb710d9mq2e t73;
            gm_f_format_dsta2ql((void *)&t73, (void *)NULL, cx, NULL, &t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 8, gm_srccfhjr4r9mdef, 261, 15);
            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
            gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
            gm_l7 = t75;
        }
        goto ix58;
ie58:;
        uint8_t t77 = 0;
        gm_b16_8 t78 = gm_l6;
        gm_b16_8 t79;
        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_soa9mnl4ahjud));
        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(7));
        uint8_t t80 = gm_f_starts_with_6vvt01e(t78, t79);
        if (!t80) {
            gm_b16_8 t81 = gm_l6;
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_s1oiqt105ulvb));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(19));
            uint8_t t83 = gm_f_starts_with_6vvt01e(t81, t82);
            t77 = t83;
        } else t77 = 1;
        if (!t77) goto ie76;
        {
            gm_ttu09lij36f8c t85;
            memset(&t85, 0, sizeof t85);
            uint8_t t86 = 0;
            F.ret = t85;
            goto gm_fret;
        }
        goto ix76;
ie76:;
ix76:;
ix58:;
    }
    {
        uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
        void * t88 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
        uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t t90 = (uintptr_t)t89;
        uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
        uint64_t t92 = (uintptr_t)t91;
        if (t90 > t92 || t92 > (uintptr_t)t87) gm_trap_slice(t90, t92, t87, gm_srccfhjr4r9mdef, 266, 13);
        gm_b16_8 t93;
        GM_ST(void *, (uint8_t *)&t93, (void *)(((uint8_t *)t88 + t90 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(t92 - t90));
        gm_l9 = t93;
    }
    {
        gm_l10 = ((int32_t)0LL);
    }
    {
        gm_tf5srh4mp4b3i t94;
        memset(&t94, 0, sizeof t94);
        gm_l11 = t94;
    }
    {
        {
            gm_b16_8 t96 = gm_l9;
            gm_l12 = t96;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t98 = gm_l13;
                uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t100 = (t98 < t99);
                if (!t100) goto lb97;
                {
                    uint64_t t103 = gm_l13;
                    uint64_t t104 = gm_l13;
                    uint64_t t105 = (uintptr_t)t104;
                    uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t107 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t105 * 1));
                    gm_tf5srh4mp4b3i t108;
                    GM_ST(uint8_t, (uint8_t *)&t108 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t108 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t108 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t108 + 0), t103);
                    GM_ST(uint8_t, ((uint8_t *)&t108 + 8), t107);
                    uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&t108) + 0));
                    gm_l14 = t111;
                    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&t108) + 8));
                    gm_l15 = t112;
                    {
                        uint8_t t115 = gm_l15;
                        uint8_t t116 = ((t115 == ((uint8_t)40ULL)));
                        if (!t116) goto ie114;
                        {
                            int32_t t118 = gm_l10;
                            int32_t r119; bool o120 = __builtin_add_overflow(t118, ((int32_t)1LL), &r119);
                            if (o120) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 271, 13);
                            int32_t t121 = r119;
                            gm_l10 = t121;
                        }
                        goto ix114;
ie114:;
                        uint8_t t123 = gm_l15;
                        uint8_t t124 = ((t123 == ((uint8_t)41ULL)));
                        if (!t124) goto ie122;
                        {
                            int32_t t126 = gm_l10;
                            int32_t r127; bool o128 = __builtin_sub_overflow(t126, ((int32_t)1LL), &r127);
                            if (o128) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 273, 13);
                            int32_t t129 = r127;
                            gm_l10 = t129;
                        }
                        goto ix122;
ie122:;
                        uint8_t t131 = gm_l15;
                        uint8_t t132 = ((t131 == ((uint8_t)123ULL)));
                        if (!t132) goto ie130;
                        int32_t t133 = gm_l10;
                        uint8_t t134 = ((t133 == ((int32_t)0LL)));
                        if (!t134) goto ie130;
                        {
                            uint64_t t136 = gm_l14;
                            gm_tf5srh4mp4b3i t137;
                            GM_ST(uint8_t, (uint8_t *)&t137 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t137 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t137 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t137), t136);
                            ((uint8_t *)&t137)[8] = 1;
                            gm_l11 = t137;
                        }
                        {
                            goto lb97;
                        }
                        goto ix130;
ie130:;
ix130:;
ix122:;
ix114:;
                    }
                    goto me109;
mn110:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 269, 5);
me109:;
                }
lc97:;
                {
                    uint64_t t139 = gm_l13;
                    uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                    uint64_t t142 = r140;
                    gm_l13 = t142;
                }
            }
lb97:;
        }
    }
    {
        gm_tf5srh4mp4b3i t143 = gm_l11;
        uint64_t t144;
        if (((((uint8_t *)&t143))[8] != 0)) {
            uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&t143) + 0));
            t144 = t145;
        } else {
            gm_ttu09lij36f8c t146;
            memset(&t146, 0, sizeof t146);
            uint8_t t147 = 0;
            F.ret = t146;
            goto gm_fret;
        }
        gm_l16 = t144;
    }
    {
        uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        void * t149 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)));
        uint64_t t150 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t151 = gm_l16;
        uint64_t t152 = (uintptr_t)t151;
        if (t150 > t152 || t152 > (uintptr_t)t148) gm_trap_slice(t150, t152, t148, gm_srccfhjr4r9mdef, 280, 28);
        gm_b16_8 t153;
        GM_ST(void *, (uint8_t *)&t153, (void *)(((uint8_t *)t149 + t150 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(t152 - t150));
        gm_b16_8 t154 = gm_f_trim_end_6sjv6rg(t153);
        gm_l17 = t154;
    }
    {
        {
            gm_b16_8 t156 = gm_l17;
            gm_l18 = t156;
        }
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t158 = gm_l19;
                uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t160 = (t158 < t159);
                if (!t160) goto lb157;
                {
                    uint64_t t163 = gm_l19;
                    uint64_t t164 = (uintptr_t)t163;
                    uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    uint8_t t166 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t164 * 1));
                    uint8_t t169 = GM_LD(uint8_t, ((uint8_t *)&t166));
                    gm_l20 = t169;
                    {
                        uint8_t t172 = gm_l20;
                        gm_toh0ootodhjue t173;
                        GM_ST(uint8_t, ((uint8_t *)&t173 + 0), ((uint8_t)10ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t173 + 1), ((uint8_t)34ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t173 + 2), ((uint8_t)39ULL));
                        uint8_t t174 = 0;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t173) + 0, ((uint8_t *)&t172))) t174 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t173) + 1, ((uint8_t *)&t172))) t174 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t173) + 2, ((uint8_t *)&t172))) t174 = 1;
                        uint8_t t175 = (uint8_t)(t174);
                        if (!t175) goto ie171;
                        {
                            gm_ttu09lij36f8c t177;
                            memset(&t177, 0, sizeof t177);
                            uint8_t t178 = 0;
                            F.ret = t177;
                            goto gm_fret;
                        }
                        goto ix171;
ie171:;
ix171:;
                    }
                    goto me167;
mn168:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 281, 5);
me167:;
                }
lc157:;
                {
                    uint64_t t180 = gm_l19;
                    uint64_t r181; bool o182 = __builtin_add_overflow(t180, ((uint64_t)1ULL), &r181);
                    uint64_t t183 = r181;
                    gm_l19 = t183;
                }
            }
lb157:;
        }
    }
    {
        gm_b16_8 t185 = gm_l17;
        gm_b16_8 t186;
        GM_ST(void *, (uint8_t *)&t186, (void *)(gm_ssi4rrma2q00g));
        GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(6));
        uint8_t t187 = gm_f_has_word_l11ltcj(t185, t186);
        if (!t187) goto ie184;
        {
            gm_ttu09lij36f8c t189;
            memset(&t189, 0, sizeof t189);
            uint8_t t190 = 0;
            F.ret = t189;
            goto gm_fret;
        }
        goto ix184;
ie184:;
ix184:;
    }
    {
        gm_b16_8 t192 = gm_l6;
        gm_b16_8 t193 = GM_LD(gm_b16_8, gm_csu9mkbsfpo);
        uint8_t t194 = gm_f_starts_with_6vvt01e(t192, t193);
        if (!t194) goto ie191;
        {
            gm_t6j6smj7qoctu t196;
            gm_b16_8 t197 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t196 + 0, t197);
            uint64_t t198 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            void * t199 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
            uint64_t t200 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t t201 = (uintptr_t)t200;
            uint64_t t202 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            uint64_t t203 = (uintptr_t)t202;
            if (t201 > t203 || t203 > (uintptr_t)t198) gm_trap_slice(t201, t203, t198, gm_srccfhjr4r9mdef, 289, 46);
            gm_b16_8 t204;
            GM_ST(void *, (uint8_t *)&t204, (void *)(((uint8_t *)t199 + t201 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(t203 - t201));
            GM_ST(gm_b16_8, (uint8_t *)&t196 + 16, t204);
            gm_tabb710d9mq2e t205;
            gm_f_format_stmnn5h((void *)&t205, (void *)NULL, cx, NULL, &t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 8, gm_srccfhjr4r9mdef, 289, 15);
            gm_b16_8 t206 = GM_LD(gm_b16_8, (((uint8_t *)&t205) + 8));
            gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&t206));
            gm_l7 = t207;
        }
        goto ix191;
ie191:;
ix191:;
    }
    {
        gm_t6j6smj7qoctu t208;
        gm_b16_8 t209 = gm_l17;
        gm_tnpifv20irnph t210;
        GM_ST(gm_b16_8, ((uint8_t *)&t210 + 0), t209);
        gm_tabb710d9mq2e t211;
        gm_f_format_dg0m8l8((void *)&t211, (void *)NULL, cx, NULL, t210);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t211))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t211) + 8, gm_srccfhjr4r9mdef, 291, 19);
        gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)&t211) + 8));
        gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&t212));
        GM_ST(gm_b16_8, (uint8_t *)&t208 + 0, t213);
        gm_b16_8 t214 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t208 + 16, t214);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 36, 0);
        ((uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out))[32] = 1;
        GM_ST(gm_t6j6smj7qoctu, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 0, t208);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_gm_names_6ner0lu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_gm_names_6ner0lu F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* c */
    uint64_t gm_l5; /* j */
    gm_b16_8 gm_l6; /* id */
    GM_STACK_CHECK(&F, 491u);
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
        gm_f_hash_map_m04clhi((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t4 = gm_l3;
            uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t6 = (t4 < t5);
            if (!t6) goto lb3;
            {
                uint64_t t9 = gm_l3;
                uint64_t t10 = (uintptr_t)t9;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srccfhjr4r9mdef, 445, 14);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t10 * 1));
                gm_l4 = t12;
            }
            {
                uint8_t t14 = 0;
                uint8_t t15 = gm_l4;
                uint8_t t16 = ((t15 == ((uint8_t)34ULL)));
                if (!t16) {
                    uint8_t t17 = gm_l4;
                    uint8_t t18 = ((t17 == ((uint8_t)39ULL)));
                    t14 = t18;
                } else t14 = 1;
                if (!t14) goto ie13;
                {
                    gm_b16_8 t20 = gm_l0;
                    uint64_t t21 = gm_l3;
                    uint64_t t22 = gm_f_skip_literal_17lq2p8(t20, t21);
                    gm_l3 = t22;
                }
                {
                    goto lc3;
                }
                goto ix13;
ie13:;
ix13:;
            }
            {
                uint8_t t24 = gm_l4;
                uint8_t t25 = gm_f_ident_char_pmpasvq(t24);
                if (!t25) goto ie23;
                {
                    uint64_t t27 = gm_l3;
                    gm_l5 = t27;
                }
                {
                    for (;;) {
                        uint64_t t29 = gm_l5;
                        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t31 = (t29 < t30);
                        if (!t31) goto lb28;
                        uint64_t t32 = gm_l5;
                        uint64_t t33 = (uintptr_t)t32;
                        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_srccfhjr4r9mdef, 452, 47);
                        uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t33 * 1));
                        uint8_t t36 = gm_f_ident_char_pmpasvq(t35);
                        if (!t36) goto lb28;
                        {
                            uint64_t t39 = gm_l5;
                            uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                            if (o41) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 452, 58);
                            uint64_t t42 = r40;
                            gm_l5 = t42;
                        }
lc28:;
                    }
lb28:;
                }
                {
                    uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t44 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t45 = gm_l3;
                    uint64_t t46 = (uintptr_t)t45;
                    uint64_t t47 = gm_l5;
                    uint64_t t48 = (uintptr_t)t47;
                    if (t46 > t48 || t48 > (uintptr_t)t43) gm_trap_slice(t46, t48, t43, gm_srccfhjr4r9mdef, 453, 19);
                    gm_b16_8 t49;
                    GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t44 + t46 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(t48 - t46));
                    gm_l6 = t49;
                }
                {
                    gm_b16_8 t51 = gm_l6;
                    gm_b16_8 t52;
                    GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s7ipvt5onv6ht));
                    GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(3));
                    uint8_t t53 = gm_f_starts_with_6vvt01e(t51, t52);
                    if (!t53) goto ie50;
                    uint8_t t54 = 0;
                    uint8_t t55 = gm_l4;
                    uint8_t t56 = (t55 >= ((uint8_t)48ULL));
                    if (t56) {
                        uint8_t t57 = gm_l4;
                        uint8_t t58 = (t57 <= ((uint8_t)57ULL));
                        t54 = t58;
                    } else t54 = 0;
                    uint8_t t59 = (uint8_t)!t54;
                    if (!t59) goto ie50;
                    void * t60 = (void *)((uint8_t *)&F.l2);
                    gm_b16_8 t61 = gm_l6;
                    gm_ttsm01t0d8srg t62 = gm_f_get_ece4opn(t60, t61);
                    uint8_t t63;
                    if (((((uint8_t *)&t62))[0] != 0)) goto mn65;
                    t63 = ((uint8_t)1);
                    goto me64;
mn65:;
                    t63 = ((uint8_t)0);
                    goto me64;
mn66:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 454, 74);
me64:;
                    if (!t63) goto ie50;
                    {
                        void * t68 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t69 = gm_l6;
                        gm_b0_1 t70;
                        gm_tftb1odc739t5 t71 = gm_f_put_drsjd1t(t68, t69, t70);
                        if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srccfhjr4r9mdef, 455, 17);
                        gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                    }
                    {
                        void * t73 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t74 = gm_l6;
                        gm_tftb1odc739t5 t75 = gm_f_push_3ef746b(t73, t74);
                        if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_srccfhjr4r9mdef, 456, 17);
                        gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
                    }
                    goto ix50;
ie50:;
ix50:;
                }
                {
                    uint64_t t77 = gm_l5;
                    gm_l3 = t77;
                }
                {
                    goto lc3;
                }
                goto ix23;
ie23:;
ix23:;
            }
            {
                uint64_t t78 = gm_l3;
                uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                if (o80) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 461, 9);
                uint64_t t81 = r79;
                gm_l3 = t81;
            }
lc3:;
        }
lb3:;
    }
    {
        gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
        uint8_t t84 = 0;
        F.ret = t83;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t84;
        goto gm_ld1_1;
    }
    {
        void * t85 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_lehld1n(t85);
    }
    goto gm_ls86;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t87 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_lehld1n(t87);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls86:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_word_l11ltcj(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_word_l11ltcj F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l1; /* word */
    uint64_t gm_l2; /* from */
    uint64_t gm_l3; /* i */
    uint64_t gm_l4; /* at */
    uint64_t gm_l5; /* end */
    GM_STACK_CHECK(&F, 352u);
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
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint64_t t13 = (uintptr_t)t12;
                if (t11 > t13 || t13 > (uintptr_t)t8) gm_trap_slice(t11, t13, t8, gm_srccfhjr4r9mdef, 298, 23);
                gm_b16_8 t14;
                GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t9 + t11 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13 - t11));
                gm_b16_8 t15 = gm_l1;
                gm_tf5srh4mp4b3i t16 = gm_f_find_nab29co(t14, t15);
                uint64_t t17;
                if (((((uint8_t *)&t16))[8] != 0)) {
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    t17 = t18;
                } else {
                    uint8_t t19 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                gm_l3 = t17;
            }
            {
                uint64_t t20 = gm_l2;
                uint64_t t21 = gm_l3;
                uint64_t r22; bool o23 = __builtin_add_overflow(t20, t21, &r22);
                if (o23) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 299, 15);
                uint64_t t24 = r22;
                gm_l4 = t24;
            }
            {
                uint64_t t25 = gm_l4;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint64_t r27; bool o28 = __builtin_add_overflow(t25, t26, &r27);
                if (o28) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 300, 16);
                uint64_t t29 = r27;
                gm_l5 = t29;
            }
            {
                uint8_t t31 = 0;
                uint64_t t32 = gm_l4;
                uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
                if (!t33) {
                    uint64_t t34 = gm_l4;
                    uint64_t r35; bool o36 = __builtin_sub_overflow(t34, ((uint64_t)1ULL), &r35);
                    if (o36) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 301, 54);
                    uint64_t t37 = r35;
                    uint64_t t38 = (uintptr_t)t37;
                    uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srccfhjr4r9mdef, 301, 49);
                    uint8_t t40 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t38 * 1));
                    uint8_t t41 = gm_f_is_ident_cont_317fpdb(t40);
                    uint8_t t42 = (uint8_t)!t41;
                    t31 = t42;
                } else t31 = 1;
                if (!t31) goto ie30;
                uint8_t t43 = 0;
                uint64_t t44 = gm_l5;
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t46 = (t44 >= t45);
                if (!t46) {
                    uint64_t t47 = gm_l5;
                    uint64_t t48 = (uintptr_t)t47;
                    uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t48 >= (uintptr_t)(t49)) gm_trap_index(t48, t49, gm_srccfhjr4r9mdef, 301, 113);
                    uint8_t t50 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t48 * 1));
                    uint8_t t51 = gm_f_is_ident_cont_317fpdb(t50);
                    uint8_t t52 = (uint8_t)!t51;
                    t43 = t52;
                } else t43 = 1;
                if (!t43) goto ie30;
                {
                    uint8_t t54 = 0;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
                goto ix30;
ie30:;
ix30:;
            }
            {
                uint64_t t55 = gm_l4;
                uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                if (o57) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 302, 16);
                uint64_t t58 = r56;
                gm_l2 = t58;
            }
lc2:;
        }
lb2:;
    }
    {
        uint8_t t59 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_ident_char_pmpasvq(uint8_t a0);
inline __attribute__((aligned(4))) void gm_f_ident_set_9ajalpn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_ident_set_9ajalpn F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* j */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_hash_map_m04clhi((void *)&F.l1, (void *)NULL, cx, NULL);
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
                uint64_t t9 = gm_l2;
                uint64_t t10 = (uintptr_t)t9;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srccfhjr4r9mdef, 725, 23);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t10 * 1));
                uint8_t t13 = gm_f_ident_char_pmpasvq(t12);
                if (!t13) goto ie8;
                {
                    uint64_t t15 = gm_l2;
                    gm_l3 = t15;
                }
                {
                    for (;;) {
                        uint64_t t17 = gm_l3;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t19 = (t17 < t18);
                        if (!t19) goto lb16;
                        uint64_t t20 = gm_l3;
                        uint64_t t21 = (uintptr_t)t20;
                        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_srccfhjr4r9mdef, 727, 47);
                        uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t21 * 1));
                        uint8_t t24 = gm_f_ident_char_pmpasvq(t23);
                        if (!t24) goto lb16;
                        {
                            uint64_t t27 = gm_l3;
                            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                            if (o29) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 727, 58);
                            uint64_t t30 = r28;
                            gm_l3 = t30;
                        }
lc16:;
                    }
lb16:;
                }
                {
                    void * t31 = (void *)((uint8_t *)&F.l1);
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t33 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t34 = gm_l2;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = gm_l3;
                    uint64_t t37 = (uintptr_t)t36;
                    if (t35 > t37 || t37 > (uintptr_t)t32) gm_trap_slice(t35, t37, t32, gm_srccfhjr4r9mdef, 728, 21);
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t33 + t35 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                    gm_b0_1 t39;
                    gm_tftb1odc739t5 t40 = gm_f_put_drsjd1t(t31, t38, t39);
                    if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srccfhjr4r9mdef, 728, 13);
                    gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                }
                {
                    uint64_t t42 = gm_l3;
                    gm_l2 = t42;
                }
                goto ix8;
ie8:;
                {
                    uint64_t t44 = gm_l2;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    if (o46) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 731, 13);
                    uint64_t t47 = r45;
                    gm_l2 = t47;
                }
ix8:;
            }
lc2:;
        }
lb2:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 56);
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
__attribute__((aligned(4))) void gm_f_inline_definition_6aokb9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_inline_definition_6aokb9d F;
    gm_b16_8 gm_l0; /* item */
    uint64_t gm_l1; /* small_bytes */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* end */
    gm_b16_8 gm_l4; /* text */
    uint64_t gm_l5; /* first_end */
    gm_b16_8 gm_l6; /* first */
    gm_b16_8 gm_l7; /* name */
    GM_STACK_CHECK(&F, 997u);
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
            uint64_t t6 = gm_l2;
            uint64_t t7 = (uintptr_t)t6;
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srccfhjr4r9mdef, 217, 32);
            uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t7 * 1));
            uint8_t t10 = ((t9 == ((uint8_t)35ULL)));
            if (!t10) goto lb2;
            {
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                void * t14 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                uint64_t t15 = gm_l2;
                uint64_t t16 = (uintptr_t)t15;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint64_t t18 = (uintptr_t)t17;
                if (t16 > t18 || t18 > (uintptr_t)t13) gm_trap_slice(t16, t18, t13, gm_srccfhjr4r9mdef, 218, 25);
                gm_b16_8 t19;
                GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t14 + t16 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(t18 - t16));
                gm_b16_8 t20;
                GM_ST(void *, (uint8_t *)&t20, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
                gm_tf5srh4mp4b3i t21 = gm_f_find_nab29co(t19, t20);
                uint64_t t22;
                if (((((uint8_t *)&t21))[8] != 0)) {
                    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&t21) + 0));
                    t22 = t23;
                } else {
                    goto lb2;
                }
                gm_l3 = t22;
            }
            {
                uint64_t t24 = gm_l2;
                uint64_t t25 = gm_l3;
                uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                if (o27) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 219, 18);
                uint64_t t28 = r26;
                uint64_t r29; bool o30 = __builtin_add_overflow(t24, t28, &r29);
                if (o30) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 219, 9);
                uint64_t t31 = r29;
                gm_l2 = t31;
            }
lc2:;
        }
lb2:;
    }
    {
        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t33 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t34 = gm_l2;
        uint64_t t35 = (uintptr_t)t34;
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t37 = (uintptr_t)t36;
        if (t35 > t37 || t37 > (uintptr_t)t32) gm_trap_slice(t35, t37, t32, gm_srccfhjr4r9mdef, 221, 13);
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t33 + t35 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
        gm_l4 = t38;
    }
    {
        gm_b16_8 t39 = gm_l4;
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t41 = gm_f_find_nab29co(t39, t40);
        uint64_t t42;
        if (((((uint8_t *)&t41))[8] != 0)) {
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&t41) + 0));
            t42 = t43;
        } else {
            uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            t42 = t44;
        }
        gm_l5 = t42;
    }
    {
        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        void * t46 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
        uint64_t t47 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t48 = gm_l5;
        uint64_t t49 = (uintptr_t)t48;
        if (t47 > t49 || t49 > (uintptr_t)t45) gm_trap_slice(t47, t49, t45, gm_srccfhjr4r9mdef, 223, 14);
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(((uint8_t *)t46 + t47 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(t49 - t47));
        gm_l6 = t50;
    }
    {
        gm_b16_8 t52 = gm_l6;
        uint8_t t53 = gm_f_is_inline_aqdknmj(t52);
        uint8_t t54 = (uint8_t)!t53;
        if (!t54) goto ie51;
        {
            uint8_t t57 = 0;
            uint8_t t58 = 0;
            uint64_t t59 = gm_l1;
            uint8_t t60 = ((t59 == ((uint64_t)0ULL)));
            if (!t60) {
                gm_b16_8 t61 = gm_l6;
                gm_b16_8 t62 = GM_LD(gm_b16_8, gm_csu9mkbsfpo);
                uint8_t t63 = gm_f_starts_with_6vvt01e(t61, t62);
                uint8_t t64 = (uint8_t)!t63;
                t58 = t64;
            } else t58 = 1;
            if (!t58) {
                gm_b16_8 t65 = gm_l0;
                uint64_t t66 = gm_f_code_bytes_u2ir6qv(t65);
                uint64_t t67 = gm_l1;
                uint8_t t68 = (t66 > t67);
                t57 = t68;
            } else t57 = 1;
            if (!t57) goto ie56;
            {
                gm_ttu09lij36f8c t70;
                memset(&t70, 0, sizeof t70);
                uint8_t t71 = 0;
                F.ret = t70;
                goto gm_fret;
            }
            goto ix56;
ie56:;
ix56:;
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        gm_b16_8 t72 = gm_l6;
        gm_th2lqbo77pvsc t73;
        gm_f_c_fn_name_77uspqp((void *)&t73, t72);
        gm_b16_8 t74;
        if (((((uint8_t *)&t73))[16] != 0)) {
            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 0));
            t74 = t75;
        } else {
            gm_ttu09lij36f8c t76;
            memset(&t76, 0, sizeof t76);
            uint8_t t77 = 0;
            F.ret = t76;
            goto gm_fret;
        }
        gm_l7 = t74;
    }
    {
        gm_t6j6smj7qoctu t78;
        gm_t6j6smj7qoctu t79;
        uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t81 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t82 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t83 = gm_l2;
        uint64_t t84 = (uintptr_t)t83;
        if (t82 > t84 || t84 > (uintptr_t)t80) gm_trap_slice(t82, t84, t80, gm_srccfhjr4r9mdef, 228, 37);
        gm_b16_8 t85;
        GM_ST(void *, (uint8_t *)&t85, (void *)(((uint8_t *)t81 + t82 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(t84 - t82));
        GM_ST(gm_b16_8, (uint8_t *)&t79 + 0, t85);
        uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        void * t87 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_soa9mnl4ahjud));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(7));
        uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&t88) + 8);
        uint64_t t90 = (uintptr_t)t89;
        uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint64_t t92 = (uintptr_t)t91;
        if (t90 > t92 || t92 > (uintptr_t)t86) gm_trap_slice(t90, t92, t86, gm_srccfhjr4r9mdef, 228, 53);
        gm_b16_8 t93;
        GM_ST(void *, (uint8_t *)&t93, (void *)(((uint8_t *)t87 + t90 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(t92 - t90));
        GM_ST(gm_b16_8, (uint8_t *)&t79 + 16, t93);
        gm_tabb710d9mq2e t94;
        gm_f_format_8n626pi((void *)&t94, (void *)NULL, cx, NULL, &t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 8, gm_srccfhjr4r9mdef, 228, 18);
        gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&t94) + 8));
        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
        GM_ST(gm_b16_8, (uint8_t *)&t78 + 0, t96);
        gm_b16_8 t97 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t78 + 16, t97);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 36, 0);
        ((uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out))[32] = 1;
        GM_ST(gm_t6j6smj7qoctu, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 0, t78);
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_inline_aqdknmj(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_items_of_q103oiv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_items_of_q103oiv F;
    gm_b16_8 gm_l0; /* code */
    uint64_t gm_l2; /* start */
    int32_t gm_l3; /* depth */
    uint64_t gm_l4; /* i */
    uint8_t gm_l5; /* line_start */
    uint8_t gm_l6; /* c */
    gm_b16_8 gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 1012u);
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
        gm_l3 = ((int32_t)0LL);
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        gm_l5 = ((uint8_t)1);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l4;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t8 = gm_l4;
                uint64_t t9 = (uintptr_t)t8;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srccfhjr4r9mdef, 316, 14);
                uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
                gm_l6 = t11;
            }
            {
                uint8_t t13 = gm_l6;
                uint8_t t14 = ((t13 == ((uint8_t)35ULL)));
                if (!t14) goto ie12;
                uint8_t t15 = gm_l5;
                if (!t15) goto ie12;
                {
                    for (;;) {
                        uint64_t t18 = gm_l4;
                        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t20 = (t18 < t19);
                        if (!t20) goto lb17;
                        uint64_t t21 = gm_l4;
                        uint64_t t22 = (uintptr_t)t21;
                        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_srccfhjr4r9mdef, 318, 36);
                        uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t22 * 1));
                        uint8_t t25 = (!(t24 == ((uint8_t)10ULL)));
                        if (!t25) goto lb17;
                        {
                            uint64_t t28 = gm_l4;
                            uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                            if (o30) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 318, 58);
                            uint64_t t31 = r29;
                            gm_l4 = t31;
                        }
lc17:;
                    }
lb17:;
                }
                {
                    goto lc2;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                uint8_t t33 = 0;
                uint8_t t34 = gm_l6;
                uint8_t t35 = ((t34 == ((uint8_t)34ULL)));
                if (!t35) {
                    uint8_t t36 = gm_l6;
                    uint8_t t37 = ((t36 == ((uint8_t)39ULL)));
                    t33 = t37;
                } else t33 = 1;
                if (!t33) goto ie32;
                {
                    uint64_t t39 = gm_l4;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    if (o41) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 322, 13);
                    uint64_t t42 = r40;
                    gm_l4 = t42;
                }
                {
                    for (;;) {
                        uint64_t t44 = gm_l4;
                        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t46 = (t44 < t45);
                        if (!t46) goto lb43;
                        uint64_t t47 = gm_l4;
                        uint64_t t48 = (uintptr_t)t47;
                        uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t48 >= (uintptr_t)(t49)) gm_trap_index(t48, t49, gm_srccfhjr4r9mdef, 323, 36);
                        uint8_t t50 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t48 * 1));
                        uint8_t t51 = gm_l6;
                        uint8_t t52 = (!(t50 == t51));
                        if (!t52) goto lb43;
                        {
                            uint64_t t55 = gm_l4;
                            uint64_t t56;
                            uint64_t t58 = gm_l4;
                            uint64_t t59 = (uintptr_t)t58;
                            uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srccfhjr4r9mdef, 324, 25);
                            uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t59 * 1));
                            uint8_t t62 = ((t61 == ((uint8_t)92ULL)));
                            if (!t62) goto ie57;
                            uint64_t t64 = ((uint64_t)2ULL);
                            t56 = t64;
                            goto ix57;
ie57:;
                            uint64_t t66 = ((uint64_t)1ULL);
                            t56 = t66;
ix57:;
                            uint64_t r67; bool o68 = __builtin_add_overflow(t55, t56, &r67);
                            if (o68) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 324, 17);
                            uint64_t t69 = r67;
                            gm_l4 = t69;
                        }
lc43:;
                    }
lb43:;
                }
                goto ix32;
ie32:;
                uint8_t t71 = gm_l6;
                uint8_t t72 = ((t71 == ((uint8_t)47ULL)));
                if (!t72) goto ie70;
                uint64_t t73 = gm_l4;
                uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                if (o75) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 326, 32);
                uint64_t t76 = r74;
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t78 = (t76 < t77);
                if (!t78) goto ie70;
                uint64_t t79 = gm_l4;
                uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                if (o81) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 326, 58);
                uint64_t t82 = r80;
                uint64_t t83 = (uintptr_t)t82;
                uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t83 >= (uintptr_t)(t84)) gm_trap_index(t83, t84, gm_srccfhjr4r9mdef, 326, 53);
                uint8_t t85 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t83 * 1));
                uint8_t t86 = ((t85 == ((uint8_t)42ULL)));
                if (!t86) goto ie70;
                {
                    uint64_t t88 = gm_l4;
                    uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)2ULL), &r89);
                    if (o90) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 327, 13);
                    uint64_t t91 = r89;
                    gm_l4 = t91;
                }
                {
                    for (;;) {
                        uint64_t t93 = gm_l4;
                        uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                        if (o95) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 328, 19);
                        uint64_t t96 = r94;
                        uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t98 = (t96 < t97);
                        if (!t98) goto lb92;
                        uint8_t t99 = 0;
                        uint64_t t100 = gm_l4;
                        uint64_t t101 = (uintptr_t)t100;
                        uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t101 >= (uintptr_t)(t102)) gm_trap_index(t101, t102, gm_srccfhjr4r9mdef, 328, 45);
                        uint8_t t103 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t101 * 1));
                        uint8_t t104 = ((t103 == ((uint8_t)42ULL)));
                        if (t104) {
                            uint64_t t105 = gm_l4;
                            uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                            if (o107) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 328, 69);
                            uint64_t t108 = r106;
                            uint64_t t109 = (uintptr_t)t108;
                            uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t109 >= (uintptr_t)(t110)) gm_trap_index(t109, t110, gm_srccfhjr4r9mdef, 328, 64);
                            uint8_t t111 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t109 * 1));
                            uint8_t t112 = ((t111 == ((uint8_t)47ULL)));
                            t99 = t112;
                        } else t99 = 0;
                        uint8_t t113 = (uint8_t)!t99;
                        if (!t113) goto lb92;
                        {
                            uint64_t t116 = gm_l4;
                            uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                            if (o118) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 328, 86);
                            uint64_t t119 = r117;
                            gm_l4 = t119;
                        }
lc92:;
                    }
lb92:;
                }
                {
                    uint64_t t120 = gm_l4;
                    uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                    if (o122) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 329, 13);
                    uint64_t t123 = r121;
                    gm_l4 = t123;
                }
                goto ix70;
ie70:;
                uint8_t t125 = gm_l6;
                uint8_t t126 = ((t125 == ((uint8_t)47ULL)));
                if (!t126) goto ie124;
                uint64_t t127 = gm_l4;
                uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                if (o129) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 330, 32);
                uint64_t t130 = r128;
                uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t132 = (t130 < t131);
                if (!t132) goto ie124;
                uint64_t t133 = gm_l4;
                uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                if (o135) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 330, 58);
                uint64_t t136 = r134;
                uint64_t t137 = (uintptr_t)t136;
                uint64_t t138 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t137 >= (uintptr_t)(t138)) gm_trap_index(t137, t138, gm_srccfhjr4r9mdef, 330, 53);
                uint8_t t139 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t137 * 1));
                uint8_t t140 = ((t139 == ((uint8_t)47ULL)));
                if (!t140) goto ie124;
                {
                    for (;;) {
                        uint64_t t143 = gm_l4;
                        uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
                        if (o145) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 331, 19);
                        uint64_t t146 = r144;
                        uint64_t t147 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t148 = (t146 < t147);
                        if (!t148) goto lb142;
                        uint64_t t149 = gm_l4;
                        uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                        if (o151) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 331, 45);
                        uint64_t t152 = r150;
                        uint64_t t153 = (uintptr_t)t152;
                        uint64_t t154 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t153 >= (uintptr_t)(t154)) gm_trap_index(t153, t154, gm_srccfhjr4r9mdef, 331, 40);
                        uint8_t t155 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t153 * 1));
                        uint8_t t156 = (!(t155 == ((uint8_t)10ULL)));
                        if (!t156) goto lb142;
                        {
                            uint64_t t159 = gm_l4;
                            uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                            if (o161) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 331, 66);
                            uint64_t t162 = r160;
                            gm_l4 = t162;
                        }
lc142:;
                    }
lb142:;
                }
                goto ix124;
ie124:;
                uint8_t t164 = gm_l6;
                uint8_t t165 = ((t164 == ((uint8_t)123ULL)));
                if (!t165) goto ie163;
                {
                    int32_t t167 = gm_l3;
                    int32_t r168; bool o169 = __builtin_add_overflow(t167, ((int32_t)1LL), &r168);
                    if (o169) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 333, 13);
                    int32_t t170 = r168;
                    gm_l3 = t170;
                }
                goto ix163;
ie163:;
                uint8_t t172 = gm_l6;
                uint8_t t173 = ((t172 == ((uint8_t)125ULL)));
                if (!t173) goto ie171;
                {
                    int32_t t175 = gm_l3;
                    int32_t r176; bool o177 = __builtin_sub_overflow(t175, ((int32_t)1LL), &r176);
                    if (o177) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 335, 13);
                    int32_t t178 = r176;
                    gm_l3 = t178;
                }
                {
                    int32_t t180 = gm_l3;
                    uint8_t t181 = (t180 < ((int32_t)0LL));
                    if (!t181) goto ie179;
                    {
                        gm_th2lqbo77pvsc t183;
                        memset(&t183, 0, sizeof t183);
                        uint8_t t184 = 0;
                        F.ret = t183;
                        goto gm_fret;
                    }
                    goto ix179;
ie179:;
ix179:;
                }
                {
                    int32_t t186 = gm_l3;
                    uint8_t t187 = ((t186 == ((int32_t)0LL)));
                    if (!t187) goto ie185;
                    uint8_t t188 = 0;
                    uint64_t t189 = gm_l4;
                    uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                    if (o191) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 337, 32);
                    uint64_t t192 = r190;
                    uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t194 = (t192 >= t193);
                    if (!t194) {
                        uint64_t t195 = gm_l4;
                        uint64_t r196; bool o197 = __builtin_add_overflow(t195, ((uint64_t)1ULL), &r196);
                        if (o197) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 337, 58);
                        uint64_t t198 = r196;
                        uint64_t t199 = (uintptr_t)t198;
                        uint64_t t200 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t199 >= (uintptr_t)(t200)) gm_trap_index(t199, t200, gm_srccfhjr4r9mdef, 337, 53);
                        uint8_t t201 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t199 * 1));
                        uint8_t t202 = ((t201 == ((uint8_t)10ULL)));
                        t188 = t202;
                    } else t188 = 1;
                    if (!t188) goto ie185;
                    {
                        uint64_t t204 = gm_l4;
                        uint64_t r205; bool o206 = __builtin_add_overflow(t204, ((uint64_t)2ULL), &r205);
                        if (o206) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 338, 22);
                        uint64_t t207 = r205;
                        uint64_t t208 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint64_t t209 = (t207 < t208 ? t207 : t208);
                        gm_l4 = t209;
                    }
                    {
                        void * t210 = (void *)((uint8_t *)&F.l1);
                        uint64_t t211 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        void * t212 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                        uint64_t t213 = gm_l2;
                        uint64_t t214 = (uintptr_t)t213;
                        uint64_t t215 = gm_l4;
                        uint64_t t216 = (uintptr_t)t215;
                        if (t214 > t216 || t216 > (uintptr_t)t211) gm_trap_slice(t214, t216, t211, gm_srccfhjr4r9mdef, 339, 28);
                        gm_b16_8 t217;
                        GM_ST(void *, (uint8_t *)&t217, (void *)(((uint8_t *)t212 + t214 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(t216 - t214));
                        gm_tftb1odc739t5 t218 = gm_f_push_3ef746b(t210, t217);
                        if (((((uint8_t *)&t218))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t218) + 1, gm_srccfhjr4r9mdef, 339, 17);
                        gm_b0_1 t219 = GM_LD(gm_b0_1, (((uint8_t *)&t218) + 1));
                    }
                    {
                        uint64_t t220 = gm_l4;
                        gm_l2 = t220;
                    }
                    {
                        gm_l5 = ((uint8_t)1);
                    }
                    {
                        goto lc2;
                    }
                    goto ix185;
ie185:;
ix185:;
                }
                goto ix171;
ie171:;
                uint8_t t222 = gm_l6;
                uint8_t t223 = ((t222 == ((uint8_t)59ULL)));
                if (!t223) goto ie221;
                int32_t t224 = gm_l3;
                uint8_t t225 = ((t224 == ((int32_t)0LL)));
                if (!t225) goto ie221;
                {
                    gm_th2lqbo77pvsc t227;
                    memset(&t227, 0, sizeof t227);
                    uint8_t t228 = 0;
                    F.ret = t227;
                    goto gm_fret;
                }
                goto ix221;
ie221:;
ix221:;
ix171:;
ix163:;
ix124:;
ix70:;
ix32:;
            }
            {
                uint8_t t229 = gm_l6;
                uint8_t t230 = ((t229 == ((uint8_t)10ULL)));
                gm_l5 = t230;
            }
            {
                uint64_t t231 = gm_l4;
                uint64_t r232; bool o233 = __builtin_add_overflow(t231, ((uint64_t)1ULL), &r232);
                if (o233) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 348, 9);
                uint64_t t234 = r232;
                gm_l4 = t234;
            }
lc2:;
        }
lb2:;
    }
    {
        {
            uint64_t t236 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t237 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t238 = gm_l2;
            uint64_t t239 = (uintptr_t)t238;
            uint64_t t240 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint64_t t241 = (uintptr_t)t240;
            if (t239 > t241 || t241 > (uintptr_t)t236) gm_trap_slice(t239, t241, t236, gm_srccfhjr4r9mdef, 351, 24);
            gm_b16_8 t242;
            GM_ST(void *, (uint8_t *)&t242, (void *)(((uint8_t *)t237 + t239 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(t241 - t239));
            gm_l7 = t242;
        }
        gm_hval t243 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn244 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl8sdnavrdjn, .env = t243.env, .hctx = t243.cx, .with_frame = &F.h, .with_label = 5, .result = (void *)&F.w5, .fw = NULL, .next = cx};
        gm_b16_8 t247 = gm_l7;
        gm_b0_1 t248 = gm_f_lines_9uns0k2_snvhnj5((void *)NULL, (&wn244), NULL, t247);
        if (gm_unw.kind) {
            goto gm_ll246;
        }
        F.w5 = t248;
        goto gm_ls249;
gm_ll246:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 5) { gm_unw.kind = GM_NONE; goto we245; }
        goto gm_lad;
gm_ls249:;
we245:;
        gm_b0_1 t250 = F.w5;
        gm_b0_1 t251 = t250;
    }
    {
        gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t252);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_line_reset_73tgnor(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_line_reset_73tgnor F;
    gm_b16_8 gm_l0; /* def */
    GM_STACK_CHECK(&F, 326u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = 0;
        gm_b16_8 t4 = gm_l0;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_spkrf9e7cr6h4));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(6));
        uint8_t t6 = gm_f_contains_gkki4kq(t4, t5);
        uint8_t t7 = (uint8_t)!t6;
        if (!t7) {
            gm_b16_8 t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, gm_ccmgjp6th6r);
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tabb710d9mq2e t11;
            gm_f_format_uaq30dv((void *)&t11, (void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_srccfhjr4r9mdef, 631, 57);
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            uint8_t t14 = gm_f_ends_with_biriu5l(t8, t13);
            t3 = t14;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_b16_8 t16 = gm_l0;
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_t6j6smj7qoctu t18;
        gm_b16_8 t19 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
        gm_b16_8 t20 = GM_LD(gm_b16_8, gm_ccmgjp6th6r);
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 16, t20);
        gm_tabb710d9mq2e t21;
        gm_f_format_q8i7gsb((void *)&t21, (void *)NULL, cx, NULL, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_srccfhjr4r9mdef, 632, 12);
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        uint8_t t24 = 0;
        F.ret = t23;
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
inline __attribute__((aligned(4))) uint64_t gm_f_optimized_unit_count_5mur81l(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_optimized_unit_count_5mur81l F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* cpus */
    gm_tf5srh4mp4b3i gm_l2; /* forced */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie2;
        uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        gm_l3 = t3;
        {
            uint64_t t5 = gm_l3;
            uint64_t t6 = (t5 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t5 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t5));
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 64) + 8);
        if (((uint64_t)4194304ULL) == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 74, 13);
        uint64_t t10 = (uint64_t)(t9 / ((uint64_t)4194304ULL));
        uint64_t t11 = gm_l1;
        uint64_t t12 = (t11 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t11 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t11));
        uint64_t t13 = (t10 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t10 > t12 ? t12 : t10));
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_set_depths_1781b1p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_set_depths_1781b1p F;
    gm_b16_8 gm_l0; /* decls */
    void * gm_l1; /* by_name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint32_t gm_l6; /* depth */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* r */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    GM_STACK_CHECK(&F, 739u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    gm_t9re6l0g6jicn t11;
                    uint64_t t12 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t11 + 0, t12);
                    uint64_t t13 = gm_l3;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    memcpy((uint8_t *)&t11 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t14 * 56), 56);
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t11) + 0));
                    gm_l4 = t18;
                    F.l5 = (*(gm_t84tkl8vf34ko *)(((uint8_t *)&t11) + 8));
                    {
                        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 48));
                        uint8_t t22 = GM_LD(uint8_t, gm_crpb2rekieb);
                        uint8_t t23 = (!(t21 == t22));
                        if (!t23) goto ie20;
                        {
                            goto lc5;
                        }
                        goto ix20;
ie20:;
ix20:;
                    }
                    {
                        gm_l6 = ((uint32_t)0ULL);
                    }
                    {
                        {
                            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 32));
                            gm_l7 = t26;
                        }
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t28 = gm_l8;
                                uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                uint8_t t30 = (t28 < t29);
                                if (!t30) goto lb27;
                                {
                                    uint64_t t33 = gm_l8;
                                    uint64_t t34 = (uintptr_t)t33;
                                    uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t34 * 16));
                                    gm_l9 = t38;
                                    {
                                        gm_b16_8 t41 = gm_l9;
                                        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
                                        uint8_t t43 = (gm_eq0iqiubfm7e(((uint8_t *)&t41), ((uint8_t *)&t42)));
                                        if (!t43) goto ie40;
                                        {
                                            goto lc27;
                                        }
                                        goto ix40;
ie40:;
ix40:;
                                    }
                                    {
                                        void * t45 = gm_l1;
                                        gm_b16_8 t46 = gm_l9;
                                        gm_tqetrgo0q24b5 t47;
                                        gm_f_get_ohh8mo3((void *)&t47, t45, t46);
                                        gm_trqfvmjtt4sij t48;
                                        if (((((uint8_t *)&t47))[40] != 0)) {
                                            t48 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t47) + 0));
                                        } else {
                                            goto lc27;
                                        }
                                        F.l10 = t48;
                                    }
                                    {
                                        {
                                            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                                            gm_l11 = t50;
                                        }
                                        {
                                            gm_l12 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t52 = gm_l12;
                                                uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                                uint8_t t54 = (t52 < t53);
                                                if (!t54) goto lb51;
                                                {
                                                    uint64_t t57 = gm_l12;
                                                    uint64_t t58 = (uintptr_t)t57;
                                                    uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                                    uint64_t t60 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t58 * 8));
                                                    uint64_t t63 = GM_LD(uint64_t, ((uint8_t *)&t60));
                                                    gm_l13 = t63;
                                                    {
                                                        uint64_t t66 = gm_l13;
                                                        uint64_t t67 = gm_l4;
                                                        uint8_t t68 = (t66 < t67);
                                                        if (!t68) goto ie65;
                                                        uint64_t t69 = gm_l13;
                                                        uint64_t t70 = (uintptr_t)t69;
                                                        uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                                        if (t70 >= (uintptr_t)(t71)) gm_trap_index(t70, t71, gm_srccfhjr4r9mdef, 645, 30);
                                                        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t70 * 56) + 48));
                                                        uint8_t t73 = GM_LD(uint8_t, gm_crpb2rekieb);
                                                        uint8_t t74 = ((t72 == t73));
                                                        if (!t74) goto ie65;
                                                        {
                                                            uint32_t t76 = gm_l6;
                                                            uint64_t t77 = gm_l13;
                                                            uint64_t t78 = (uintptr_t)t77;
                                                            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                                            if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_srccfhjr4r9mdef, 645, 77);
                                                            uint32_t t80 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t78 * 56) + 52));
                                                            uint32_t r81; bool o82 = __builtin_add_overflow(t80, ((uint32_t)1ULL), &r81);
                                                            if (o82) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 645, 77);
                                                            uint32_t t83 = r81;
                                                            uint32_t t84 = (t76 > t83 ? t76 : t83);
                                                            gm_l6 = t84;
                                                        }
                                                        goto ix65;
ie65:;
ix65:;
                                                    }
                                                    goto me61;
mn62:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 644, 13);
me61:;
                                                }
lc51:;
                                                {
                                                    uint64_t t86 = gm_l12;
                                                    uint64_t r87; bool o88 = __builtin_add_overflow(t86, ((uint64_t)1ULL), &r87);
                                                    uint64_t t89 = r87;
                                                    gm_l12 = t89;
                                                }
                                            }
lb51:;
                                        }
                                    }
                                    goto me36;
mn37:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 641, 9);
me36:;
                                }
lc27:;
                                {
                                    uint64_t t91 = gm_l8;
                                    uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
                                    uint64_t t94 = r92;
                                    gm_l8 = t94;
                                }
                            }
lb27:;
                        }
                    }
                    {
                        uint64_t t95 = gm_l4;
                        uint64_t t96 = (uintptr_t)t95;
                        uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t96 >= (uintptr_t)(t97)) gm_trap_index(t96, t97, gm_srccfhjr4r9mdef, 648, 9);
                        uint32_t t98 = gm_l6;
                        GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t96 * 56) + 52), t98);
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 638, 5);
me16:;
                }
lc5:;
                {
                    uint64_t t100 = gm_l3;
                    uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                    uint64_t t103 = r101;
                    gm_l3 = t103;
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
inline __attribute__((aligned(4))) uint64_t gm_f_skip_literal_17lq2p8(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_literal_17lq2p8 F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* i */
    uint8_t gm_l2; /* q */
    uint64_t gm_l3; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 233u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t3 >= (uintptr_t)(t4)) gm_trap_index(t3, t4, gm_srccfhjr4r9mdef, 427, 10);
        uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t3 * 1));
        gm_l2 = t5;
    }
    {
        uint64_t t6 = gm_l1;
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 428, 10);
        uint64_t t9 = r7;
        gm_l3 = t9;
    }
    {
        for (;;) {
            uint64_t t11 = gm_l3;
            uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t13 = (t11 < t12);
            if (!t13) goto lb10;
            uint64_t t14 = gm_l3;
            uint64_t t15 = (uintptr_t)t14;
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srccfhjr4r9mdef, 429, 28);
            uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t15 * 1));
            uint8_t t18 = gm_l2;
            uint8_t t19 = (!(t17 == t18));
            if (!t19) goto lb10;
            {
                uint64_t t22 = gm_l3;
                uint64_t t23;
                uint64_t t25 = gm_l3;
                uint64_t t26 = (uintptr_t)t25;
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srccfhjr4r9mdef, 430, 17);
                uint8_t t28 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t26 * 1));
                uint8_t t29 = ((t28 == ((uint8_t)92ULL)));
                if (!t29) goto ie24;
                uint64_t t31 = ((uint64_t)2ULL);
                t23 = t31;
                goto ix24;
ie24:;
                uint64_t t33 = ((uint64_t)1ULL);
                t23 = t33;
ix24:;
                uint64_t r34; bool o35 = __builtin_add_overflow(t22, t23, &r34);
                if (o35) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 430, 9);
                uint64_t t36 = r34;
                gm_l3 = t36;
            }
lc10:;
        }
lb10:;
    }
    {
        uint64_t t37 = gm_l3;
        uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
        if (o39) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 432, 13);
        uint64_t t40 = r38;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t42 = (t40 < t41 ? t40 : t41);
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sort_name_ro82p6b(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_name_ro82p6b F;
    void * gm_l0; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 16) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 16));
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 32) + 8);
        uint8_t t13 = (t12 > ((uint64_t)0ULL));
        if (!t13) goto ie10;
        {
            uint64_t t15 = (uintptr_t)((uint64_t)0ULL);
            void * t16 = gm_l0;
            uint64_t t17 = GM_LD(uintptr_t, (((uint8_t *)t16) + 32) + 8);
            if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srccfhjr4r9mdef, 716, 32);
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t16) + 32)) + t15 * 16));
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(0));
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_split_by_file_hgjqgsp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_split_by_file_hgjqgsp F;
    void * gm_l0; /* parts */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t6j6smj7qoctu gm_l6; /* o */
    gm_b16_8 gm_l8; /* all */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l11; /* item */
    gm_t6j6smj7qoctu gm_l12; /* d */
    gm_t6j6smj7qoctu gm_l13; /* f */
    gm_b16_8 gm_l14; /* name */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_b16_8 gm_l22; /* <tmp> */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* i */
    void * gm_l29; /* l */
    gm_b16_8 gm_l32; /* first */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    gm_b16_8 gm_l39; /* def */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* k */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    gm_t6j6smj7qoctu gm_l49; /* f */
    GM_STACK_CHECK(&F, 3796u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 96) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) goto ie2;
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 64) + 8);
        uint8_t t8 = (t7 > ((uint64_t)0ULL));
        if (!t8) goto ie2;
        {
            gm_ttu09lij36f8c t10;
            memset(&t10, 0, sizeof t10);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_list_016fo30((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = (void *)((uint8_t *)&F.l1);
        void * t13 = gm_l0;
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 128));
        uint8_t t15 = GM_LD(uint8_t, gm_crpb2rekieb);
        gm_b0_1 t16 = gm_f_decls_of_srep9du((void *)NULL, cx, NULL, t12, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t17 = (void *)((uint8_t *)&F.l1);
        void * t18 = gm_l0;
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t18) + 144));
        uint8_t t20 = GM_LD(uint8_t, gm_cos4c858gsq);
        gm_b0_1 t21 = gm_f_decls_of_srep9du((void *)NULL, cx, NULL, t17, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_hash_map_lljdp3h((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        gm_f_list_1g734tk((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            void * t24 = gm_l0;
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t24) + 96));
            gm_l4 = t25;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t27 = gm_l5;
                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t29 = (t27 < t28);
                if (!t29) goto lb26;
                {
                    uint64_t t32 = gm_l5;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t6j6smj7qoctu t37 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t33 * 32));
                    gm_l6 = t37;
                    {
                        gm_f_list_c8kt75n((void *)&F.l7, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll30;
                        }
                    }
                    {
                        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 16));
                        gm_th2lqbo77pvsc t40;
                        gm_f_items_of_q103oiv((void *)&t40, (void *)NULL, cx, NULL, t39);
                        if (gm_unw.kind) {
                            goto gm_ll30;
                        }
                        gm_b16_8 t41;
                        if (((((uint8_t *)&t40))[16] != 0)) {
                            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 0));
                            t41 = t42;
                        } else {
                            gm_ttu09lij36f8c t43;
                            memset(&t43, 0, sizeof t43);
                            uint8_t t44 = 0;
                            F.ret = t43;
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t44;
                            goto gm_ll30;
                        }
                        gm_l8 = t41;
                    }
                    {
                        {
                            gm_b16_8 t46 = gm_l8;
                            gm_l9 = t46;
                        }
                        {
                            gm_l10 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t48 = gm_l10;
                                uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                uint8_t t50 = (t48 < t49);
                                if (!t50) goto lb47;
                                {
                                    uint64_t t53 = gm_l10;
                                    uint64_t t54 = (uintptr_t)t53;
                                    uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                    gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t54 * 16));
                                    gm_l11 = t58;
                                    {
                                        gm_b16_8 t61 = gm_l11;
                                        gm_ttu09lij36f8c t62;
                                        gm_f_inline_definition_6aokb9d((void *)&t62, (void *)NULL, cx, NULL, t61, ((uint64_t)1600ULL));
                                        if (gm_unw.kind) {
                                            goto gm_ll51;
                                        }
                                        if (!((((uint8_t *)&t62))[32] != 0)) goto ie60;
                                        gm_l12 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t62) + 0));
                                        {
                                            void * t64 = (void *)((uint8_t *)&F.l2);
                                            gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                                            uint64_t t66 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                                            gm_tftb1odc739t5 t67 = gm_f_put_qoojoqv(t64, t65, t66);
                                            if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 1, gm_srccfhjr4r9mdef, 542, 17);
                                            gm_b0_1 t68 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 1));
                                        }
                                        {
                                            void * t69 = (void *)((uint8_t *)&F.l1);
                                            gm_t84tkl8vf34ko t70;
                                            memset((uint8_t *)&t70 + 49, 0, 3);
                                            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                                            gm_b16_8 t72 = gm_f_line_reset_73tgnor((void *)NULL, cx, NULL, t71);
                                            if (gm_unw.kind) {
                                                goto gm_ll51;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t70 + 0, t72);
                                            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                                            GM_ST(gm_b16_8, (uint8_t *)&t70 + 16, t73);
                                            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                                            gm_b16_8 t75 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t74);
                                            if (gm_unw.kind) {
                                                goto gm_ll51;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t70 + 32, t75);
                                            uint8_t t76 = GM_LD(uint8_t, gm_c30tdq2p48i);
                                            GM_ST(uint8_t, (uint8_t *)&t70 + 48, t76);
                                            GM_ST(uint32_t, (uint8_t *)&t70 + 52, ((uint32_t)0ULL));
                                            gm_tftb1odc739t5 t77 = gm_f_push_rda4o9n(t69, &t70);
                                            if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_srccfhjr4r9mdef, 543, 17);
                                            gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
                                        }
                                        {
                                            goto lc47;
                                        }
                                        goto ix60;
ie60:;
ix60:;
                                    }
                                    {
                                        gm_b16_8 t79 = gm_l11;
                                        gm_ttu09lij36f8c t80;
                                        gm_f_function_o2lv8g6((void *)&t80, (void *)NULL, cx, NULL, t79);
                                        if (gm_unw.kind) {
                                            goto gm_ll51;
                                        }
                                        gm_t6j6smj7qoctu t81;
                                        if (((((uint8_t *)&t80))[32] != 0)) {
                                            t81 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t80) + 0));
                                        } else {
                                            gm_ttu09lij36f8c t82;
                                            memset(&t82, 0, sizeof t82);
                                            uint8_t t83 = 0;
                                            F.ret = t82;
                                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t83;
                                            goto gm_ll51;
                                        }
                                        gm_l13 = t81;
                                    }
                                    {
                                        gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                                        gm_th2lqbo77pvsc t85;
                                        gm_f_c_fn_name_77uspqp((void *)&t85, t84);
                                        gm_b16_8 t86;
                                        if (((((uint8_t *)&t85))[16] != 0)) {
                                            gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t85) + 0));
                                            t86 = t87;
                                        } else {
                                            gm_ttu09lij36f8c t88;
                                            memset(&t88, 0, sizeof t88);
                                            uint8_t t89 = 0;
                                            F.ret = t88;
                                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t89;
                                            goto gm_ll51;
                                        }
                                        gm_l14 = t86;
                                    }
                                    {
                                        void * t90 = (void *)((uint8_t *)&F.l7);
                                        gm_t6j6smj7qoctu t91;
                                        gm_b16_8 t92 = gm_l14;
                                        GM_ST(gm_b16_8, (uint8_t *)&t91 + 0, t92);
                                        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 16));
                                        gm_b16_8 t94 = gm_f_line_reset_73tgnor((void *)NULL, cx, NULL, t93);
                                        if (gm_unw.kind) {
                                            goto gm_ll51;
                                        }
                                        GM_ST(gm_b16_8, (uint8_t *)&t91 + 16, t94);
                                        gm_tftb1odc739t5 t95 = gm_f_push_0ek3ea4(t90, &t91);
                                        if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_srccfhjr4r9mdef, 548, 13);
                                        gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
                                    }
                                    {
                                        void * t97 = (void *)((uint8_t *)&F.l1);
                                        gm_t84tkl8vf34ko t98;
                                        memset((uint8_t *)&t98 + 49, 0, 3);
                                        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                                        gm_tnpifv20irnph t100;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t100 + 0), t99);
                                        gm_tabb710d9mq2e t101;
                                        gm_f_format_uaq30dv((void *)&t101, (void *)NULL, cx, NULL, t100);
                                        if (gm_unw.kind) {
                                            goto gm_ll51;
                                        }
                                        if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 8, gm_srccfhjr4r9mdef, 549, 35);
                                        gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&t101) + 8));
                                        gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
                                        GM_ST(gm_b16_8, (uint8_t *)&t98 + 0, t103);
                                        gm_b16_8 t104 = gm_l14;
                                        GM_ST(gm_b16_8, (uint8_t *)&t98 + 16, t104);
                                        gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                                        gm_b16_8 t106 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t105);
                                        if (gm_unw.kind) {
                                            goto gm_ll51;
                                        }
                                        GM_ST(gm_b16_8, (uint8_t *)&t98 + 32, t106);
                                        uint8_t t107 = GM_LD(uint8_t, gm_c72j1g8d455);
                                        GM_ST(uint8_t, (uint8_t *)&t98 + 48, t107);
                                        GM_ST(uint32_t, (uint8_t *)&t98 + 52, ((uint32_t)0ULL));
                                        gm_tftb1odc739t5 t108 = gm_f_push_rda4o9n(t97, &t98);
                                        if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_srccfhjr4r9mdef, 549, 13);
                                        gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
                                    }
                                    goto me56;
mn57:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 540, 9);
me56:;
                                }
lc47:;
                                {
                                    uint64_t t111 = gm_l10;
                                    uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                                    uint64_t t114 = r112;
                                    gm_l10 = t114;
                                }
                            }
gm_ll51:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb47; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc47; }
                            }
                            goto gm_ll30;
lb47:;
                        }
                    }
                    {
                        void * t115 = (void *)((uint8_t *)&F.l3);
                        gm_tftb1odc739t5 t116 = gm_f_push_rlpbnuq(t115, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l7)));
                        if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 1, gm_srccfhjr4r9mdef, 551, 9);
                        gm_b0_1 t117 = GM_LD(gm_b0_1, (((uint8_t *)&t116) + 1));
                    }
                    goto me35;
mn36:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 537, 5);
me35:;
                }
lc26:;
                {
                    uint64_t t119 = gm_l5;
                    uint64_t r120; bool o121 = __builtin_add_overflow(t119, ((uint64_t)1ULL), &r120);
                    uint64_t t122 = r120;
                    gm_l5 = t122;
                }
            }
gm_ll30:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb26; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc26; }
            }
            goto gm_ld1_1;
lb26:;
        }
    }
    {
        gm_f_list_1g734tk((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_l16 = t124;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t126 = gm_l17;
                uint64_t t127 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t128 = (t126 < t127);
                if (!t128) goto lb125;
                {
                    uint64_t t131 = gm_l17;
                    uint64_t t132 = (uintptr_t)t131;
                    uint64_t t133 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    {
                        void * t137 = (void *)((uint8_t *)&F.l15);
                        gm_trqfvmjtt4sij t138;
                        gm_f_list_c8kt75n((void *)&t138, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll129;
                        }
                        gm_tftb1odc739t5 t139 = gm_f_push_rlpbnuq(t137, &t138);
                        if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 1, gm_srccfhjr4r9mdef, 554, 29);
                        gm_b0_1 t140 = GM_LD(gm_b0_1, (((uint8_t *)&t139) + 1));
                    }
                    goto me134;
mn135:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 554, 5);
me134:;
                }
lc125:;
                {
                    uint64_t t142 = gm_l17;
                    uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                    uint64_t t145 = r143;
                    gm_l17 = t145;
                }
            }
gm_ll129:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb125; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc125; }
            }
            goto gm_ld1_1;
lb125:;
        }
    }
    {
        {
            void * t147 = gm_l0;
            gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)t147) + 16));
            gm_l18 = t148;
        }
        gm_hval t149 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn150 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl1l1s81m49k, .env = t149.env, .hctx = t149.cx, .with_frame = &F.h, .with_label = 3, .result = (void *)&F.w3, .fw = NULL, .next = cx};
        gm_b16_8 t153 = gm_l18;
        gm_b0_1 t154 = gm_f_lines_9uns0k2_svejqbu((void *)NULL, (&wn150), NULL, t153);
        if (gm_unw.kind) {
            goto gm_ll152;
        }
        F.w3 = t154;
        goto gm_ls155;
gm_ll152:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 3) { gm_unw.kind = GM_NONE; goto we151; }
        goto gm_ld1_1;
gm_ls155:;
we151:;
        gm_b0_1 t156 = F.w3;
        gm_b0_1 t157 = t156;
    }
    {
        gm_f_list_b7l80f8((void *)&F.l19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            gm_l21 = ((uint64_t)0ULL);
        }
        {
            gm_l20 = ((uint64_t)16ULL);
        }
        {
            for (;;) {
                uint64_t t160 = gm_l21;
                uint64_t t161 = gm_l20;
                uint8_t t162 = (t160 < t161);
                if (!t162) goto lb159;
                {
                    uint64_t t165 = gm_l21;
                    {
                        void * t169 = (void *)((uint8_t *)&F.l19);
                        gm_trqfvmjtt4sij t170;
                        gm_f_list_epvducg((void *)&t170, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll163;
                        }
                        gm_tftb1odc739t5 t171 = gm_f_push_vnejmrp(t169, &t170);
                        if (((((uint8_t *)&t171))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t171) + 1, gm_srccfhjr4r9mdef, 571, 31);
                        gm_b0_1 t172 = GM_LD(gm_b0_1, (((uint8_t *)&t171) + 1));
                    }
                    goto me166;
mn167:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 571, 5);
me166:;
                }
lc159:;
                {
                    uint64_t t174 = gm_l21;
                    uint64_t r175; bool o176 = __builtin_add_overflow(t174, ((uint64_t)1ULL), &r175);
                    uint64_t t177 = r175;
                    gm_l21 = t177;
                }
            }
gm_ll163:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb159; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc159; }
            }
            goto gm_ld1_1;
lb159:;
        }
    }
    {
        {
            void * t179 = gm_l0;
            gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)t179) + 32));
            gm_l22 = t180;
        }
        gm_hval t181 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn182 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlth8t37utgd, .env = t181.env, .hctx = t181.cx, .with_frame = &F.h, .with_label = 5, .result = (void *)&F.w5, .fw = NULL, .next = cx};
        gm_b16_8 t185 = gm_l22;
        gm_b0_1 t186 = gm_f_lines_9uns0k2_s1f7rak((void *)NULL, (&wn182), NULL, t185);
        if (gm_unw.kind) {
            goto gm_ll184;
        }
        F.w5 = t186;
        goto gm_ls187;
gm_ll184:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 5) { gm_unw.kind = GM_NONE; goto we183; }
        goto gm_ld1_1;
gm_ls187:;
we183:;
        gm_b0_1 t188 = F.w5;
        gm_b0_1 t189 = t188;
    }
    {
        gm_f_hash_map_dklib0j((void *)&F.l23, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l24, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
        {
            gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
            gm_l25 = t193;
        }
        {
            gm_l26 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t195 = gm_l26;
                uint64_t t196 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                uint8_t t197 = (t195 < t196);
                if (!t197) goto lb194;
                {
                    gm_t9re6l0g6jicn t200;
                    uint64_t t201 = gm_l26;
                    GM_ST(uint64_t, (uint8_t *)&t200 + 0, t201);
                    uint64_t t202 = gm_l26;
                    uint64_t t203 = (uintptr_t)t202;
                    uint64_t t204 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                    memcpy((uint8_t *)&t200 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t203 * 56), 56);
                    uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&t200) + 0));
                    gm_l27 = t207;
                    F.l28 = (*(gm_t84tkl8vf34ko *)(((uint8_t *)&t200) + 8));
                    {
                        uint64_t t210 = GM_LD(uintptr_t, (((uint8_t *)&F.l28) + 16) + 8);
                        uint8_t t211 = ((t210 == ((uint64_t)0ULL)));
                        if (!t211) goto ie209;
                        {
                            void * t213 = (void *)((uint8_t *)&F.l24);
                            uint64_t t214 = gm_l27;
                            gm_tftb1odc739t5 t215 = gm_f_push_ucgmlo2(t213, t214);
                            if (((((uint8_t *)&t215))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t215) + 1, gm_srccfhjr4r9mdef, 590, 13);
                            gm_b0_1 t216 = GM_LD(gm_b0_1, (((uint8_t *)&t215) + 1));
                        }
                        {
                            goto lc194;
                        }
                        goto ix209;
ie209:;
ix209:;
                    }
                    {
                        void * t218 = (void *)((uint8_t *)&F.l23);
                        gm_b16_8 t219 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 16));
                        void * t220 = gm_f_get_ptr_53uc4fm(t218, t219);
                        if (!(GM_LD(void *, ((uint8_t *)&t220)) != 0)) goto ie217;
                        void * t221 = GM_LD(void *, ((uint8_t *)&t220));
                        gm_l29 = t221;
                        {
                            void * t223 = gm_l29;
                            uint64_t t224 = gm_l27;
                            gm_tftb1odc739t5 t225 = gm_f_push_ucgmlo2(t223, t224);
                            if (((((uint8_t *)&t225))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t225) + 1, gm_srccfhjr4r9mdef, 594, 13);
                            gm_b0_1 t226 = GM_LD(gm_b0_1, (((uint8_t *)&t225) + 1));
                        }
                        goto ix217;
ie217:;
                        {
                            gm_f_list_5dhgg4i((void *)&F.l30, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll198;
                            }
                        }
                        {
                            void * t228 = (void *)((uint8_t *)&F.l30);
                            uint64_t t229 = gm_l27;
                            gm_tftb1odc739t5 t230 = gm_f_push_ucgmlo2(t228, t229);
                            if (((((uint8_t *)&t230))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t230) + 1, gm_srccfhjr4r9mdef, 597, 13);
                            gm_b0_1 t231 = GM_LD(gm_b0_1, (((uint8_t *)&t230) + 1));
                        }
                        {
                            void * t232 = (void *)((uint8_t *)&F.l23);
                            gm_b16_8 t233 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 16));
                            gm_tftb1odc739t5 t234 = gm_f_put_7o5vpta(t232, t233, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l30)));
                            if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 1, gm_srccfhjr4r9mdef, 598, 13);
                            gm_b0_1 t235 = GM_LD(gm_b0_1, (((uint8_t *)&t234) + 1));
                        }
ix217:;
                    }
                    goto me205;
mn206:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 588, 5);
me205:;
                }
lc194:;
                {
                    uint64_t t237 = gm_l26;
                    uint64_t r238; bool o239 = __builtin_add_overflow(t237, ((uint64_t)1ULL), &r238);
                    uint64_t t240 = r238;
                    gm_l26 = t240;
                }
            }
gm_ll198:;
            if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb194; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc194; }
            }
            goto gm_ld1_2;
lb194:;
        }
    }
    {
        gm_b16_8 t241 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        void * t242 = (void *)((uint8_t *)&F.l23);
        gm_b0_1 t243 = gm_f_set_depths_1781b1p((void *)NULL, cx, NULL, t241, t242);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l31, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
        gm_t6j6smj7qoctu t244;
        void * t245 = gm_l0;
        gm_b16_8 t246 = GM_LD(gm_b16_8, (((uint8_t *)t245) + 48));
        GM_ST(gm_b16_8, (uint8_t *)&t244 + 0, t246);
        void * t247 = gm_l0;
        gm_b16_8 t248 = GM_LD(gm_b16_8, (((uint8_t *)t247) + 80));
        GM_ST(gm_b16_8, (uint8_t *)&t244 + 16, t248);
        gm_tabb710d9mq2e t249;
        gm_f_format_8n626pi((void *)&t249, (void *)NULL, cx, NULL, &t244);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
        if (((((uint8_t *)&t249))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t249) + 8, gm_srccfhjr4r9mdef, 603, 14);
        gm_b16_8 t250 = GM_LD(gm_b16_8, (((uint8_t *)&t249) + 8));
        gm_l32 = t250;
    }
    {
        void * t251 = (void *)((uint8_t *)&F.l31);
        gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)&t252));
        void * t254 = (void *)((uint8_t *)&F.l23);
        gm_b16_8 t255 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
        gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&t255));
        gm_b16_8 t257 = gm_l32;
        gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)&t257));
        void * t259 = gm_l0;
        gm_b16_8 t260 = GM_LD(gm_b16_8, (((uint8_t *)t259) + 160));
        gm_b16_8 t261 = gm_f_unit_text_mmejmp8((void *)NULL, cx, NULL, t253, t254, t256, t258, t260);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
        gm_tftb1odc739t5 t262 = gm_f_push_3ef746b(t251, t261);
        if (((((uint8_t *)&t262))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t262) + 1, gm_srccfhjr4r9mdef, 604, 5);
        gm_b0_1 t263 = GM_LD(gm_b0_1, (((uint8_t *)&t262) + 1));
    }
    {
        {
            gm_b16_8 t265 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
            gm_l33 = t265;
        }
        {
            gm_l34 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t267 = gm_l34;
                uint64_t t268 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                uint8_t t269 = (t267 < t268);
                if (!t269) goto lb266;
                {
                    uint64_t t272 = gm_l34;
                    uint64_t t273 = (uintptr_t)t272;
                    uint64_t t274 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                    gm_trqfvmjtt4sij t277 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t273 * 40));
                    F.l35 = t277;
                    {
                        uint64_t t280 = GM_LD(uintptr_t, (((uint8_t *)&F.l35) + 0) + 8);
                        uint8_t t281 = ((t280 == ((uint64_t)0ULL)));
                        if (!t281) goto ie279;
                        {
                            goto lc266;
                        }
                        goto ix279;
ie279:;
ix279:;
                    }
                    {
                        gm_b16_8 t283 = GM_LD(gm_b16_8, (((uint8_t *)&F.l35) + 0));
                        gm_f_sort_skeree7(t283);
                    }
                    {
                        gm_f_list_uvfu1k1((void *)&F.l36, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll270;
                        }
                    }
                    {
                        {
                            gm_b16_8 t285 = GM_LD(gm_b16_8, (((uint8_t *)&F.l35) + 0));
                            gm_l37 = t285;
                        }
                        {
                            gm_l38 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t287 = gm_l38;
                                uint64_t t288 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                                uint8_t t289 = (t287 < t288);
                                if (!t289) goto lb286;
                                {
                                    uint64_t t292 = gm_l38;
                                    uint64_t t293 = (uintptr_t)t292;
                                    uint64_t t294 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                                    gm_b16_8 t297 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l37)) + t293 * 16));
                                    gm_l39 = t297;
                                    {
                                        void * t299 = (void *)((uint8_t *)&F.l36);
                                        gm_b16_8 t300 = gm_l39;
                                        gm_tnpifv20irnph t301;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t301 + 0), t300);
                                        gm_tftb1odc739t5 t302 = gm_f_write_cdouvhd(t299, t301);
                                        if (((((uint8_t *)&t302))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t302) + 1, gm_srccfhjr4r9mdef, 610, 30);
                                        gm_b0_1 t303 = GM_LD(gm_b0_1, (((uint8_t *)&t302) + 1));
                                    }
                                    goto me295;
mn296:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 610, 9);
me295:;
                                }
lc286:;
                                {
                                    uint64_t t305 = gm_l38;
                                    uint64_t r306; bool o307 = __builtin_add_overflow(t305, ((uint64_t)1ULL), &r306);
                                    uint64_t t308 = r306;
                                    gm_l38 = t308;
                                }
                            }
lb286:;
                        }
                    }
                    {
                        void * t309 = (void *)((uint8_t *)&F.l31);
                        gm_b16_8 t310 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
                        gm_b16_8 t311 = GM_LD(gm_b16_8, ((uint8_t *)&t310));
                        void * t312 = (void *)((uint8_t *)&F.l23);
                        gm_b16_8 t313 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                        gm_b16_8 t314 = GM_LD(gm_b16_8, ((uint8_t *)&t313));
                        gm_b16_8 t315 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 0));
                        gm_b16_8 t316 = GM_LD(gm_b16_8, ((uint8_t *)&t315));
                        void * t317 = gm_l0;
                        gm_b16_8 t318 = GM_LD(gm_b16_8, (((uint8_t *)t317) + 160));
                        gm_b16_8 t319 = gm_f_unit_text_mmejmp8((void *)NULL, cx, NULL, t311, t312, t314, t316, t318);
                        if (gm_unw.kind) {
                            goto gm_ll270;
                        }
                        gm_tftb1odc739t5 t320 = gm_f_push_3ef746b(t309, t319);
                        if (((((uint8_t *)&t320))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t320) + 1, gm_srccfhjr4r9mdef, 611, 9);
                        gm_b0_1 t321 = GM_LD(gm_b0_1, (((uint8_t *)&t320) + 1));
                    }
                    goto me275;
mn276:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 605, 5);
me275:;
                }
lc266:;
                {
                    uint64_t t323 = gm_l34;
                    uint64_t r324; bool o325 = __builtin_add_overflow(t323, ((uint64_t)1ULL), &r324);
                    uint64_t t326 = r324;
                    gm_l34 = t326;
                }
            }
gm_ll270:;
            if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb266; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc266; }
            }
            goto gm_ld1_2;
lb266:;
        }
    }
    {
        {
            gm_b16_8 t328 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t329 = GM_LD(gm_b16_8, ((uint8_t *)&t328));
            gm_l40 = t329;
        }
        {
            gm_l41 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t331 = gm_l41;
                uint64_t t332 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                uint8_t t333 = (t331 < t332);
                if (!t333) goto lb330;
                {
                    gm_tmupb6k0asuti t336;
                    uint64_t t337 = gm_l41;
                    GM_ST(uint64_t, (uint8_t *)&t336 + 0, t337);
                    uint64_t t338 = gm_l41;
                    uint64_t t339 = (uintptr_t)t338;
                    uint64_t t340 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                    memcpy((uint8_t *)&t336 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t339 * 40), 40);
                    uint64_t t343 = GM_LD(uint64_t, (((uint8_t *)&t336) + 0));
                    gm_l42 = t343;
                    F.l43 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t336) + 8));
                    {
                        uint64_t t345 = gm_l42;
                        uint64_t t346 = (uintptr_t)t345;
                        uint64_t t347 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                        if (t346 >= (uintptr_t)(t347)) gm_trap_index(t346, t347, gm_srccfhjr4r9mdef, 614, 16);
                        gm_trqfvmjtt4sij t348 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t346 * 40));
                        F.l44 = t348;
                    }
                    {
                        uint64_t t350 = GM_LD(uintptr_t, (((uint8_t *)&F.l43) + 0) + 8);
                        uint8_t t351 = ((t350 == ((uint64_t)0ULL)));
                        if (!t351) goto ie349;
                        uint64_t t352 = GM_LD(uintptr_t, (((uint8_t *)&F.l44) + 0) + 8);
                        uint8_t t353 = ((t352 == ((uint64_t)0ULL)));
                        if (!t353) goto ie349;
                        {
                            goto lc330;
                        }
                        goto ix349;
ie349:;
ix349:;
                    }
                    {
                        gm_f_list_c8kt75n((void *)&F.l45, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll334;
                        }
                    }
                    {
                        void * t355 = (void *)((uint8_t *)&F.l45);
                        gm_b16_8 t356 = GM_LD(gm_b16_8, (((uint8_t *)&F.l43) + 0));
                        gm_b16_8 t357 = GM_LD(gm_b16_8, ((uint8_t *)&t356));
                        gm_tftb1odc739t5 t358 = gm_f_extend_k51ik07(t355, t357);
                        if (((((uint8_t *)&t358))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t358) + 1, gm_srccfhjr4r9mdef, 617, 9);
                        gm_b0_1 t359 = GM_LD(gm_b0_1, (((uint8_t *)&t358) + 1));
                    }
                    {
                        void * t360 = (void *)((uint8_t *)&F.l45);
                        gm_b16_8 t361 = GM_LD(gm_b16_8, (((uint8_t *)&F.l44) + 0));
                        gm_b16_8 t362 = GM_LD(gm_b16_8, ((uint8_t *)&t361));
                        gm_tftb1odc739t5 t363 = gm_f_extend_k51ik07(t360, t362);
                        if (((((uint8_t *)&t363))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t363) + 1, gm_srccfhjr4r9mdef, 618, 9);
                        gm_b0_1 t364 = GM_LD(gm_b0_1, (((uint8_t *)&t363) + 1));
                    }
                    {
                        gm_b16_8 t365 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 0));
                        gm_closure t366 = ((gm_closure){GM_PTR_TAG(gm_f_fn_idfg0st), NULL, cx});
                        gm_f_sort_by_4j6ktaf(t365, t366);
                        if (gm_unw.kind) {
                            goto gm_ll334;
                        }
                    }
                    {
                        gm_f_list_uvfu1k1((void *)&F.l46, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll334;
                        }
                    }
                    {
                        {
                            gm_b16_8 t368 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 0));
                            gm_l47 = t368;
                        }
                        {
                            gm_l48 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t370 = gm_l48;
                                uint64_t t371 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                                uint8_t t372 = (t370 < t371);
                                if (!t372) goto lb369;
                                {
                                    uint64_t t375 = gm_l48;
                                    uint64_t t376 = (uintptr_t)t375;
                                    uint64_t t377 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                                    gm_t6j6smj7qoctu t380 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l47)) + t376 * 32));
                                    gm_l49 = t380;
                                    {
                                        void * t382 = (void *)((uint8_t *)&F.l46);
                                        gm_b16_8 t383 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l49) + 16));
                                        gm_tftb1odc739t5 t384 = gm_f_extend_0cs85gs(t382, t383);
                                        if (((((uint8_t *)&t384))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t384) + 1, gm_srccfhjr4r9mdef, 621, 30);
                                        gm_b0_1 t385 = GM_LD(gm_b0_1, (((uint8_t *)&t384) + 1));
                                    }
                                    goto me378;
mn379:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 621, 9);
me378:;
                                }
lc369:;
                                {
                                    uint64_t t387 = gm_l48;
                                    uint64_t r388; bool o389 = __builtin_add_overflow(t387, ((uint64_t)1ULL), &r388);
                                    uint64_t t390 = r388;
                                    gm_l48 = t390;
                                }
                            }
lb369:;
                        }
                    }
                    {
                        void * t391 = (void *)((uint8_t *)&F.l31);
                        gm_b16_8 t392 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
                        gm_b16_8 t393 = GM_LD(gm_b16_8, ((uint8_t *)&t392));
                        void * t394 = (void *)((uint8_t *)&F.l23);
                        gm_b16_8 t395 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                        gm_b16_8 t396 = GM_LD(gm_b16_8, ((uint8_t *)&t395));
                        gm_b16_8 t397 = GM_LD(gm_b16_8, (((uint8_t *)&F.l46) + 0));
                        gm_b16_8 t398 = GM_LD(gm_b16_8, ((uint8_t *)&t397));
                        void * t399 = gm_l0;
                        gm_b16_8 t400 = GM_LD(gm_b16_8, (((uint8_t *)t399) + 160));
                        gm_b16_8 t401 = gm_f_unit_text_mmejmp8((void *)NULL, cx, NULL, t393, t394, t396, t398, t400);
                        if (gm_unw.kind) {
                            goto gm_ll334;
                        }
                        gm_tftb1odc739t5 t402 = gm_f_push_3ef746b(t391, t401);
                        if (((((uint8_t *)&t402))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t402) + 1, gm_srccfhjr4r9mdef, 622, 9);
                        gm_b0_1 t403 = GM_LD(gm_b0_1, (((uint8_t *)&t402) + 1));
                    }
                    goto me341;
mn342:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 613, 5);
me341:;
                }
lc330:;
                {
                    uint64_t t405 = gm_l41;
                    uint64_t r406; bool o407 = __builtin_add_overflow(t405, ((uint64_t)1ULL), &r406);
                    uint64_t t408 = r406;
                    gm_l41 = t408;
                }
            }
gm_ll334:;
            if (gm_unw.frame == &F.h && gm_unw.label == 9) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb330; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc330; }
            }
            goto gm_ld1_2;
lb330:;
        }
    }
    {
        GM_ST(uint8_t, (uint8_t *)&F.ret + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&F.ret + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&F.ret + 36, 0);
        ((uint8_t *)&F.ret)[32] = 1;
        void * t409 = gm_l0;
        gm_b16_8 t410 = GM_LD(gm_b16_8, (((uint8_t *)t409) + 112));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6j6smj7qoctu *)((uint8_t *)&F.ret + 0)) + 0, t410);
        gm_b16_8 t411 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6j6smj7qoctu *)((uint8_t *)&F.ret + 0)) + 16, t411);
        uint8_t t412 = 0;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t412;
        goto gm_ld1_2;
    }
    {
        void * t413 = (void *)((uint8_t *)&F.l23);
        gm_f_deinit_vtu6170(t413);
    }
    {
        void * t414 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_nvuvq05(t414);
    }
    goto gm_ls415;
gm_ld1_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t416 = (void *)((uint8_t *)&F.l23);
        gm_f_deinit_vtu6170(t416);
        gm_unw = gm_saved;
    }
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t417 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_nvuvq05(t417);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls415:;
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
inline __attribute__((aligned(4))) void gm_f_split_jendjdm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_split_jendjdm F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* n */
    gm_t6j6smj7qoctu gm_l2; /* s */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* u */
    GM_STACK_CHECK(&F, 436u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_ttu09lij36f8c t4;
        gm_f_split_shared_a91530r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t5;
        if (((((uint8_t *)&t4))[32] != 0)) {
            t5 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t4) + 0));
        } else {
            gm_th2lqbo77pvsc t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        gm_f_list_epvducg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 16));
            gm_l4 = t9;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t11 = gm_l5;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l5;
                    uint64_t t17 = (uintptr_t)t16;
                    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t17 * 16));
                    gm_l6 = t21;
                    {
                        void * t23 = (void *)((uint8_t *)&F.l3);
                        gm_t6j6smj7qoctu t24;
                        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t24 + 0, t25);
                        gm_b16_8 t26 = gm_l6;
                        GM_ST(gm_b16_8, (uint8_t *)&t24 + 16, t26);
                        gm_tabb710d9mq2e t27;
                        gm_f_format_8n626pi((void *)&t27, (void *)NULL, cx, NULL, &t24);
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_srccfhjr4r9mdef, 90, 33);
                        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
                        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                        gm_tftb1odc739t5 t30 = gm_f_push_3ef746b(t23, t29);
                        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srccfhjr4r9mdef, 90, 24);
                        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 90, 5);
me19:;
                }
lc10:;
                {
                    uint64_t t33 = gm_l5;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l5 = t36;
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
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t37);
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
__attribute__((aligned(4))) void gm_f_split_shared_a91530r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_split_shared_a91530r F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* all */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* item */
    gm_t6j6smj7qoctu gm_l8; /* d */
    gm_b16_8 gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* <tmp> */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l17; /* item */
    gm_t6j6smj7qoctu gm_l18; /* f */
    gm_b16_8 gm_l19; /* shared */
    uint64_t gm_l20; /* total */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_b16_8 gm_l23; /* item */
    uint64_t gm_l24; /* share */
    uint64_t gm_l27; /* size */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_b16_8 gm_l30; /* item */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    GM_STACK_CHECK(&F, 2105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 64));
        gm_th2lqbo77pvsc t4;
        gm_f_items_of_q103oiv((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_ttu09lij36f8c t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        gm_f_hash_map_m04clhi((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            gm_b16_8 t11 = gm_l2;
            gm_l5 = t11;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l6;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l6;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t19 * 16));
                    gm_l7 = t23;
                    {
                        gm_b16_8 t26 = gm_l7;
                        gm_ttu09lij36f8c t27;
                        gm_f_inline_definition_6aokb9d((void *)&t27, (void *)NULL, cx, NULL, t26, ((uint64_t)0ULL));
                        if (gm_unw.kind) {
                            goto gm_ll16;
                        }
                        if (!((((uint8_t *)&t27))[32] != 0)) goto ie25;
                        gm_l8 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t27) + 0));
                        {
                            void * t29 = (void *)((uint8_t *)&F.l4);
                            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                            gm_tftb1odc739t5 t31 = gm_f_extend_0cs85gs(t29, t30);
                            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srccfhjr4r9mdef, 109, 13);
                            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                        }
                        {
                            void * t33 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 16));
                            gm_b0_1 t35;
                            gm_tftb1odc739t5 t36 = gm_f_put_drsjd1t(t33, t34, t35);
                            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srccfhjr4r9mdef, 110, 13);
                            gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 107, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t39 = gm_l6;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    uint64_t t42 = r40;
                    gm_l6 = t42;
                }
            }
gm_ll16:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
            }
            goto gm_ld1_1;
lb12:;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            void * t44 = gm_l0;
            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)t44) + 16));
            gm_l12 = t45;
        }
        gm_hval t46 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn47 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlrddq2io9s1, .env = t46.env, .hctx = t46.cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
        gm_b16_8 t50 = gm_l12;
        gm_b0_1 t51 = gm_f_lines_9uns0k2_s1hv6k0((void *)NULL, (&wn47), NULL, t50);
        if (gm_unw.kind) {
            goto gm_ll49;
        }
        F.w1 = t51;
        goto gm_ls52;
gm_ll49:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we48; }
        goto gm_ld1_1;
gm_ls52:;
we48:;
        gm_b0_1 t53 = F.w1;
        gm_b0_1 t54 = t53;
    }
    {
        {
            void * t56 = gm_l0;
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)t56) + 32));
            gm_l13 = t57;
        }
        gm_hval t58 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn59 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlc7jtja1te5, .env = t58.env, .hctx = t58.cx, .with_frame = &F.h, .with_label = 2, .result = (void *)&F.w2, .fw = NULL, .next = cx};
        gm_b16_8 t62 = gm_l13;
        gm_b0_1 t63 = gm_f_lines_9uns0k2_sp8kip0((void *)NULL, (&wn59), NULL, t62);
        if (gm_unw.kind) {
            goto gm_ll61;
        }
        F.w2 = t63;
        goto gm_ls64;
gm_ll61:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 2) { gm_unw.kind = GM_NONE; goto we60; }
        goto gm_ld1_1;
gm_ls64:;
we60:;
        gm_b0_1 t65 = F.w2;
        gm_b0_1 t66 = t65;
    }
    {
        gm_f_list_epvducg((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            gm_b16_8 t68 = gm_l2;
            gm_l15 = t68;
        }
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t70 = gm_l16;
                uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t72 = (t70 < t71);
                if (!t72) goto lb69;
                {
                    uint64_t t75 = gm_l16;
                    uint64_t t76 = (uintptr_t)t75;
                    uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t76 * 16));
                    gm_l17 = t80;
                    {
                        gm_b16_8 t83 = gm_l17;
                        gm_ttu09lij36f8c t84;
                        gm_f_inline_definition_6aokb9d((void *)&t84, (void *)NULL, cx, NULL, t83, ((uint64_t)0ULL));
                        if (gm_unw.kind) {
                            goto gm_ll73;
                        }
                        uint8_t t85;
                        if (((((uint8_t *)&t84))[32] != 0)) goto mn87;
                        t85 = ((uint8_t)0);
                        goto me86;
mn87:;
                        t85 = ((uint8_t)1);
                        goto me86;
mn88:;
                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 143, 12);
me86:;
                        if (!t85) goto ie82;
                        {
                            goto lc69;
                        }
                        goto ix82;
ie82:;
ix82:;
                    }
                    {
                        gm_b16_8 t90 = gm_l17;
                        gm_ttu09lij36f8c t91;
                        gm_f_function_o2lv8g6((void *)&t91, (void *)NULL, cx, NULL, t90);
                        if (gm_unw.kind) {
                            goto gm_ll73;
                        }
                        gm_t6j6smj7qoctu t92;
                        if (((((uint8_t *)&t91))[32] != 0)) {
                            t92 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t91) + 0));
                        } else {
                            gm_ttu09lij36f8c t93;
                            memset(&t93, 0, sizeof t93);
                            uint8_t t94 = 0;
                            F.ret = t93;
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t94;
                            goto gm_ll73;
                        }
                        gm_l18 = t92;
                    }
                    {
                        void * t95 = (void *)((uint8_t *)&F.l9);
                        gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                        gm_tftb1odc739t5 t97 = gm_f_extend_0cs85gs(t95, t96);
                        if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 1, gm_srccfhjr4r9mdef, 145, 9);
                        gm_b0_1 t98 = GM_LD(gm_b0_1, (((uint8_t *)&t97) + 1));
                    }
                    {
                        void * t99 = (void *)((uint8_t *)&F.l9);
                        gm_tftb1odc739t5 t100 = gm_f_push_t83rg2m(t99, ((uint8_t)10ULL));
                        if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_srccfhjr4r9mdef, 146, 9);
                        gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
                    }
                    {
                        void * t102 = (void *)((uint8_t *)&F.l14);
                        gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 16));
                        gm_tftb1odc739t5 t104 = gm_f_push_3ef746b(t102, t103);
                        if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_srccfhjr4r9mdef, 147, 9);
                        gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
                    }
                    goto me78;
mn79:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 142, 5);
me78:;
                }
lc69:;
                {
                    uint64_t t107 = gm_l16;
                    uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                    uint64_t t110 = r108;
                    gm_l16 = t110;
                }
            }
gm_ll73:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb69; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc69; }
            }
            goto gm_ld1_1;
lb69:;
        }
    }
    {
        gm_tfeab279c16rr t111;
        void * t112 = gm_l0;
        gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)t112) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t111 + 0, t113);
        gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t111 + 16, t114);
        gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t111 + 32, t115);
        gm_tabb710d9mq2e t116;
        gm_f_format_c2lphrf((void *)&t116, (void *)NULL, cx, NULL, &t111);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 8, gm_srccfhjr4r9mdef, 149, 15);
        gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&t116) + 8));
        gm_l19 = t117;
    }
    {
        gm_l20 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_l21 = t119;
        }
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t121 = gm_l22;
                uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                uint8_t t123 = (t121 < t122);
                if (!t123) goto lb120;
                {
                    uint64_t t126 = gm_l22;
                    uint64_t t127 = (uintptr_t)t126;
                    uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t127 * 16));
                    gm_l23 = t131;
                    {
                        uint64_t t133 = gm_l20;
                        uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                        uint64_t r135; bool o136 = __builtin_add_overflow(t133, t134, &r135);
                        if (o136) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 151, 31);
                        uint64_t t137 = r135;
                        gm_l20 = t137;
                    }
                    goto me129;
mn130:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 151, 5);
me129:;
                }
lc120:;
                {
                    uint64_t t139 = gm_l22;
                    uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                    uint64_t t142 = r140;
                    gm_l22 = t142;
                }
            }
lb120:;
        }
    }
    {
        uint64_t t143 = gm_l20;
        uint64_t t144 = gm_l1;
        uint64_t t145 = (t144 > ((uint64_t)1ULL) ? t144 : ((uint64_t)1ULL));
        uint64_t r146; bool o147 = __builtin_add_overflow(t143, t145, &r146);
        if (o147) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 152, 16);
        uint64_t t148 = r146;
        uint64_t r149; bool o150 = __builtin_sub_overflow(t148, ((uint64_t)1ULL), &r149);
        if (o150) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 152, 16);
        uint64_t t151 = r149;
        uint64_t t152 = gm_l1;
        uint64_t t153 = (t152 > ((uint64_t)1ULL) ? t152 : ((uint64_t)1ULL));
        if (t153 == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 152, 15);
        uint64_t t154 = (uint64_t)(t151 / t153);
        uint64_t t155 = (t154 > ((uint64_t)1ULL) ? t154 : ((uint64_t)1ULL));
        gm_l24 = t155;
    }
    {
        gm_f_list_lj5vg1g((void *)&F.l25, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t156 = (void *)((uint8_t *)&F.l26);
        gm_tfeab279c16rr t157;
        void * t158 = gm_l0;
        gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)t158) + 48));
        GM_ST(gm_b16_8, (uint8_t *)&t157 + 0, t159);
        gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t157 + 16, t160);
        gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t157 + 32, t161);
        gm_tftb1odc739t5 t162 = gm_f_write_qm3m7a0(t156, &t157);
        if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 1, gm_srccfhjr4r9mdef, 155, 5);
        gm_b0_1 t163 = GM_LD(gm_b0_1, (((uint8_t *)&t162) + 1));
    }
    {
        void * t164 = (void *)((uint8_t *)&F.l25);
        gm_tftb1odc739t5 t165 = gm_f_push_97b8r3k(t164, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l26)));
        if (((((uint8_t *)&t165))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t165) + 1, gm_srccfhjr4r9mdef, 156, 5);
        gm_b0_1 t166 = GM_LD(gm_b0_1, (((uint8_t *)&t165) + 1));
    }
    {
        gm_l27 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_l28 = t168;
        }
        {
            gm_l29 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t170 = gm_l29;
                uint64_t t171 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                uint8_t t172 = (t170 < t171);
                if (!t172) goto lb169;
                {
                    uint64_t t175 = gm_l29;
                    uint64_t t176 = (uintptr_t)t175;
                    uint64_t t177 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                    gm_b16_8 t180 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t176 * 16));
                    gm_l30 = t180;
                    {
                        uint64_t t183 = gm_l27;
                        uint64_t t184 = gm_l24;
                        uint8_t t185 = (t183 >= t184);
                        if (!t185) goto ie182;
                        uint64_t t186 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
                        uint64_t t187 = gm_l1;
                        uint8_t t188 = (t186 < t187);
                        if (!t188) goto ie182;
                        {
                            void * t190 = (void *)((uint8_t *)&F.l25);
                            gm_trqfvmjtt4sij t191;
                            gm_f_list_uvfu1k1((void *)&t191, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll173;
                            }
                            gm_tftb1odc739t5 t192 = gm_f_push_97b8r3k(t190, &t191);
                            if (((((uint8_t *)&t192))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t192) + 1, gm_srccfhjr4r9mdef, 160, 13);
                            gm_b0_1 t193 = GM_LD(gm_b0_1, (((uint8_t *)&t192) + 1));
                        }
                        {
                            gm_l27 = ((uint64_t)0ULL);
                        }
                        goto ix182;
ie182:;
ix182:;
                    }
                    {
                        uint64_t t194 = gm_l27;
                        uint64_t t195 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                        uint64_t r196; bool o197 = __builtin_add_overflow(t194, t195, &r196);
                        if (o197) gm_trap("integer overflow in `+`", gm_srccfhjr4r9mdef, 163, 9);
                        uint64_t t198 = r196;
                        gm_l27 = t198;
                    }
                    {
                        uint64_t t199 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
                        uint64_t r200; bool o201 = __builtin_sub_overflow(t199, ((uint64_t)1ULL), &r200);
                        if (o201) gm_trap("integer overflow in `-`", gm_srccfhjr4r9mdef, 164, 21);
                        uint64_t t202 = r200;
                        uint64_t t203 = (uintptr_t)t202;
                        uint64_t t204 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
                        if (t203 >= (uintptr_t)(t204)) gm_trap_index(t203, t204, gm_srccfhjr4r9mdef, 164, 9);
                        void * t205 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l25) + 0)) + t203 * 40);
                        gm_b16_8 t206 = gm_l30;
                        gm_tftb1odc739t5 t207 = gm_f_extend_0cs85gs(t205, t206);
                        if (((((uint8_t *)&t207))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t207) + 1, gm_srccfhjr4r9mdef, 164, 9);
                        gm_b0_1 t208 = GM_LD(gm_b0_1, (((uint8_t *)&t207) + 1));
                    }
                    goto me178;
mn179:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 158, 5);
me178:;
                }
lc169:;
                {
                    uint64_t t210 = gm_l29;
                    uint64_t r211; bool o212 = __builtin_add_overflow(t210, ((uint64_t)1ULL), &r211);
                    uint64_t t213 = r211;
                    gm_l29 = t213;
                }
            }
gm_ll173:;
            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb169; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc169; }
            }
            goto gm_ld1_1;
lb169:;
        }
    }
    {
        uint64_t t214 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t215 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
        if (t214 >= (uintptr_t)(t215)) gm_trap_index(t214, t215, gm_srccfhjr4r9mdef, 166, 5);
        void * t216 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l25) + 0)) + t214 * 40);
        void * t217 = gm_l0;
        gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)t217) + 80));
        gm_tftb1odc739t5 t219 = gm_f_extend_0cs85gs(t216, t218);
        if (((((uint8_t *)&t219))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t219) + 1, gm_srccfhjr4r9mdef, 166, 5);
        gm_b0_1 t220 = GM_LD(gm_b0_1, (((uint8_t *)&t219) + 1));
    }
    {
        gm_f_list_epvducg((void *)&F.l31, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
            gm_l32 = t222;
        }
        {
            gm_l33 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t224 = gm_l33;
                uint64_t t225 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                uint8_t t226 = (t224 < t225);
                if (!t226) goto lb223;
                {
                    uint64_t t229 = gm_l33;
                    uint64_t t230 = (uintptr_t)t229;
                    uint64_t t231 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    gm_trqfvmjtt4sij t234 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t230 * 40));
                    F.l34 = t234;
                    {
                        void * t236 = (void *)((uint8_t *)&F.l31);
                        gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
                        gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&t237));
                        gm_tftb1odc739t5 t239 = gm_f_push_3ef746b(t236, t238);
                        if (((((uint8_t *)&t239))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t239) + 1, gm_srccfhjr4r9mdef, 168, 28);
                        gm_b0_1 t240 = GM_LD(gm_b0_1, (((uint8_t *)&t239) + 1));
                    }
                    goto me232;
mn233:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 168, 5);
me232:;
                }
lc223:;
                {
                    uint64_t t242 = gm_l33;
                    uint64_t r243; bool o244 = __builtin_add_overflow(t242, ((uint64_t)1ULL), &r243);
                    uint64_t t245 = r243;
                    gm_l33 = t245;
                }
            }
lb223:;
        }
    }
    {
        GM_ST(uint8_t, (uint8_t *)&F.ret + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&F.ret + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&F.ret + 36, 0);
        ((uint8_t *)&F.ret)[32] = 1;
        gm_b16_8 t246 = gm_l19;
        gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)&t246));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6j6smj7qoctu *)((uint8_t *)&F.ret + 0)) + 0, t247);
        gm_b16_8 t248 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6j6smj7qoctu *)((uint8_t *)&F.ret + 0)) + 16, t248);
        uint8_t t249 = 0;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t249;
        goto gm_ld1_1;
    }
    {
        void * t250 = (void *)((uint8_t *)&F.l3);
        gm_f_deinit_lehld1n(t250);
    }
    goto gm_ls251;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t252 = (void *)((uint8_t *)&F.l3);
        gm_f_deinit_lehld1n(t252);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls251:;
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
extern __attribute__((aligned(4))) uint64_t gm_f_unit_count_dk0d2e4(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_unit_text_mmejmp8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_unit_text_mmejmp8 F;
    void * gm_l1; /* by_name */
    gm_b16_8 gm_l2; /* nameless */
    gm_b16_8 gm_l3; /* body */
    gm_b16_8 gm_l4; /* members */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l10; /* w */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* k */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l17; /* r */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* k */
    gm_b16_8 gm_l21; /* refs */
    uint8_t gm_l22; /* all */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_b16_8 gm_l25; /* r */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* k */
    uint8_t gm_l30; /* t */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* k */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_b16_8 gm_l40; /* m */
    GM_STACK_CHECK(&F, 2221u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_f_list_s83jn2q((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = F.l0;
            gm_l6 = t3;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l7;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l7;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    {
                        void * t16 = (void *)((uint8_t *)&F.l5);
                        gm_tftb1odc739t5 t17 = gm_f_push_vd1290p(t16, ((uint8_t)0));
                        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srccfhjr4r9mdef, 656, 22);
                        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 656, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t20 = gm_l7;
                    uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                    uint64_t t23 = r21;
                    gm_l7 = t23;
                }
            }
lb4:;
        }
    }
    {
        gm_f_hash_map_m04clhi((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        gm_f_list_epvducg((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t26 = gm_l3;
        gm_b16_8 t27 = gm_f_gm_names_6ner0lu((void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_tftb1odc739t5 t28 = gm_f_extend_vogsd72(t25, t27);
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srccfhjr4r9mdef, 660, 5);
        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
    }
    {
        for (;;) {
            void * t31 = (void *)((uint8_t *)&F.l9);
            gm_th2lqbo77pvsc t32;
            gm_f_pop_vr08sii((void *)&t32, t31);
            if (!((((uint8_t *)&t32))[16] != 0)) goto lb30;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            gm_l10 = t33;
            {
                void * t37 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t38 = gm_l10;
                gm_ttsm01t0d8srg t39 = gm_f_get_ece4opn(t37, t38);
                uint8_t t40;
                if (((((uint8_t *)&t39))[0] != 0)) goto mn42;
                t40 = ((uint8_t)0);
                goto me41;
mn42:;
                t40 = ((uint8_t)1);
                goto me41;
mn43:;
                gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 662, 12);
me41:;
                if (!t40) goto ie36;
                {
                    goto lc30;
                }
                goto ix36;
ie36:;
ix36:;
            }
            {
                void * t45 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t46 = gm_l10;
                gm_b0_1 t47;
                gm_tftb1odc739t5 t48 = gm_f_put_drsjd1t(t45, t46, t47);
                if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srccfhjr4r9mdef, 663, 9);
                gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
            }
            {
                void * t50 = gm_l1;
                gm_b16_8 t51 = gm_l10;
                gm_tqetrgo0q24b5 t52;
                gm_f_get_ohh8mo3((void *)&t52, t50, t51);
                gm_trqfvmjtt4sij t53;
                if (((((uint8_t *)&t52))[40] != 0)) {
                    t53 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t52) + 0));
                } else {
                    goto lc30;
                }
                F.l11 = t53;
            }
            {
                {
                    gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
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
                            uint64_t t65 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t63 * 8));
                            uint64_t t68 = GM_LD(uint64_t, ((uint8_t *)&t65));
                            gm_l14 = t68;
                            {
                                uint64_t t71 = gm_l14;
                                uint64_t t72 = (uintptr_t)t71;
                                uint64_t t73 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                if (t72 >= (uintptr_t)(t73)) gm_trap_index(t72, t73, gm_srccfhjr4r9mdef, 666, 16);
                                uint8_t t74 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t72 * 1));
                                if (!t74) goto ie70;
                                {
                                    goto lc56;
                                }
                                goto ix70;
ie70:;
ix70:;
                            }
                            {
                                uint64_t t76 = gm_l14;
                                uint64_t t77 = (uintptr_t)t76;
                                uint64_t t78 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                if (t77 >= (uintptr_t)(t78)) gm_trap_index(t77, t78, gm_srccfhjr4r9mdef, 667, 13);
                                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t77 * 1), ((uint8_t)1));
                            }
                            {
                                {
                                    uint64_t t80 = gm_l14;
                                    uint64_t t81 = (uintptr_t)t80;
                                    uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
                                    if (t81 >= (uintptr_t)(t82)) gm_trap_index(t81, t82, gm_srccfhjr4r9mdef, 668, 22);
                                    gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l0)) + t81 * 56) + 32));
                                    gm_l15 = t83;
                                }
                                {
                                    gm_l16 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t85 = gm_l16;
                                        uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                        uint8_t t87 = (t85 < t86);
                                        if (!t87) goto lb84;
                                        {
                                            uint64_t t90 = gm_l16;
                                            uint64_t t91 = (uintptr_t)t90;
                                            uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                            gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t91 * 16));
                                            gm_l17 = t95;
                                            {
                                                void * t98 = (void *)((uint8_t *)&F.l8);
                                                gm_b16_8 t99 = gm_l17;
                                                gm_ttsm01t0d8srg t100 = gm_f_get_ece4opn(t98, t99);
                                                uint8_t t101;
                                                if (((((uint8_t *)&t100))[0] != 0)) goto mn103;
                                                t101 = ((uint8_t)1);
                                                goto me102;
mn103:;
                                                t101 = ((uint8_t)0);
                                                goto me102;
mn104:;
                                                gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 668, 41);
me102:;
                                                if (!t101) goto ie97;
                                                {
                                                    void * t106 = (void *)((uint8_t *)&F.l9);
                                                    gm_b16_8 t107 = gm_l17;
                                                    gm_tftb1odc739t5 t108 = gm_f_push_3ef746b(t106, t107);
                                                    if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_srccfhjr4r9mdef, 668, 66);
                                                    gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
                                                }
                                                goto ix97;
ie97:;
ix97:;
                                            }
                                            goto me93;
mn94:;
                                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 668, 13);
me93:;
                                        }
lc84:;
                                        {
                                            uint64_t t111 = gm_l16;
                                            uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                                            uint64_t t114 = r112;
                                            gm_l16 = t114;
                                        }
                                    }
lb84:;
                                }
                            }
                            goto me66;
mn67:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 665, 9);
me66:;
                        }
lc56:;
                        {
                            uint64_t t116 = gm_l13;
                            uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                            uint64_t t119 = r117;
                            gm_l13 = t119;
                        }
                    }
lb56:;
                }
            }
lc30:;
        }
lb30:;
    }
    {
        {
            gm_b16_8 t121 = gm_l2;
            gm_l18 = t121;
        }
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t123 = gm_l19;
                uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t125 = (t123 < t124);
                if (!t125) goto lb122;
                {
                    uint64_t t128 = gm_l19;
                    uint64_t t129 = (uintptr_t)t128;
                    uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    uint64_t t131 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t129 * 8));
                    uint64_t t134 = GM_LD(uint64_t, ((uint8_t *)&t131));
                    gm_l20 = t134;
                    {
                        uint64_t t136 = gm_l20;
                        uint64_t t137 = (uintptr_t)t136;
                        uint64_t t138 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
                        if (t137 >= (uintptr_t)(t138)) gm_trap_index(t137, t138, gm_srccfhjr4r9mdef, 673, 17);
                        gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l0)) + t137 * 56) + 32));
                        gm_l21 = t139;
                    }
                    {
                        uint64_t t141 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                        uint8_t t142 = ((t141 == ((uint64_t)0ULL)));
                        if (!t142) goto ie140;
                        {
                            goto lc122;
                        }
                        goto ix140;
ie140:;
ix140:;
                    }
                    {
                        gm_l22 = ((uint8_t)1);
                    }
                    {
                        {
                            gm_b16_8 t145 = gm_l21;
                            gm_l23 = t145;
                        }
                        {
                            gm_l24 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t147 = gm_l24;
                                uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                uint8_t t149 = (t147 < t148);
                                if (!t149) goto lb146;
                                {
                                    uint64_t t152 = gm_l24;
                                    uint64_t t153 = (uintptr_t)t152;
                                    uint64_t t154 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                    gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t153 * 16));
                                    gm_l25 = t157;
                                    {
                                        void * t160 = gm_l1;
                                        gm_b16_8 t161 = gm_l25;
                                        gm_tqetrgo0q24b5 t162;
                                        gm_f_get_ohh8mo3((void *)&t162, t160, t161);
                                        uint8_t t163;
                                        if (((((uint8_t *)&t162))[40] != 0)) goto mn165;
                                        t163 = ((uint8_t)0);
                                        goto me164;
mn165:;
                                        t163 = ((uint8_t)1);
                                        goto me164;
mn166:;
                                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 677, 16);
me164:;
                                        if (!t163) goto ie159;
                                        void * t167 = (void *)((uint8_t *)&F.l8);
                                        gm_b16_8 t168 = gm_l25;
                                        gm_ttsm01t0d8srg t169 = gm_f_get_ece4opn(t167, t168);
                                        uint8_t t170;
                                        if (((((uint8_t *)&t169))[0] != 0)) goto mn172;
                                        t170 = ((uint8_t)1);
                                        goto me171;
mn172:;
                                        t170 = ((uint8_t)0);
                                        goto me171;
mn173:;
                                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 677, 47);
me171:;
                                        if (!t170) goto ie159;
                                        {
                                            gm_l22 = ((uint8_t)0);
                                        }
                                        goto ix159;
ie159:;
ix159:;
                                    }
                                    goto me155;
mn156:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 676, 9);
me155:;
                                }
lc146:;
                                {
                                    uint64_t t176 = gm_l24;
                                    uint64_t r177; bool o178 = __builtin_add_overflow(t176, ((uint64_t)1ULL), &r177);
                                    uint64_t t179 = r177;
                                    gm_l24 = t179;
                                }
                            }
lb146:;
                        }
                    }
                    {
                        uint8_t t181 = gm_l22;
                        if (!t181) goto ie180;
                        {
                            uint64_t t183 = gm_l20;
                            uint64_t t184 = (uintptr_t)t183;
                            uint64_t t185 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                            if (t184 >= (uintptr_t)(t185)) gm_trap_index(t184, t185, gm_srccfhjr4r9mdef, 679, 18);
                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t184 * 1), ((uint8_t)1));
                        }
                        goto ix180;
ie180:;
ix180:;
                    }
                    goto me132;
mn133:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 672, 5);
me132:;
                }
lc122:;
                {
                    uint64_t t187 = gm_l19;
                    uint64_t r188; bool o189 = __builtin_add_overflow(t187, ((uint64_t)1ULL), &r188);
                    uint64_t t190 = r188;
                    gm_l19 = t190;
                }
            }
lb122:;
        }
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        {
            gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
            gm_l27 = t193;
        }
        {
            gm_l28 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t195 = gm_l28;
                uint64_t t196 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                uint8_t t197 = (t195 < t196);
                if (!t197) goto lb194;
                {
                    uint64_t t200 = gm_l28;
                    uint64_t t201 = gm_l28;
                    uint64_t t202 = (uintptr_t)t201;
                    uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                    uint8_t t204 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t202 * 1));
                    gm_tf5srh4mp4b3i t205;
                    GM_ST(uint8_t, (uint8_t *)&t205 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t205 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t205 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t205 + 0), t200);
                    GM_ST(uint8_t, ((uint8_t *)&t205 + 8), t204);
                    uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&t205) + 0));
                    gm_l29 = t208;
                    uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)&t205) + 8));
                    gm_l30 = t209;
                    {
                        uint8_t t212 = gm_l30;
                        if (!t212) goto ie211;
                        {
                            void * t214 = (void *)((uint8_t *)&F.l26);
                            uint64_t t215 = gm_l29;
                            gm_tftb1odc739t5 t216 = gm_f_push_ucgmlo2(t214, t215);
                            if (((((uint8_t *)&t216))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t216) + 1, gm_srccfhjr4r9mdef, 682, 49);
                            gm_b0_1 t217 = GM_LD(gm_b0_1, (((uint8_t *)&t216) + 1));
                        }
                        goto ix211;
ie211:;
ix211:;
                    }
                    goto me206;
mn207:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 682, 5);
me206:;
                }
lc194:;
                {
                    uint64_t t219 = gm_l28;
                    uint64_t r220; bool o221 = __builtin_add_overflow(t219, ((uint64_t)1ULL), &r220);
                    uint64_t t222 = r220;
                    gm_l28 = t222;
                }
            }
lb194:;
        }
    }
    {
        gm_b16_8 t223 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
        gm_closure t224 = ((gm_closure){(void *)gm_f_fn_575gb0c, (void *)&F.h, cx});
        gm_f_sort_by_08k4kf2(t223, t224);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l31, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_f_hash_map_m04clhi((void *)&F.l32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
    }
    {
        {
            gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
            gm_l33 = t227;
        }
        {
            gm_l34 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t229 = gm_l34;
                uint64_t t230 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                uint8_t t231 = (t229 < t230);
                if (!t231) goto lb228;
                {
                    uint64_t t234 = gm_l34;
                    uint64_t t235 = (uintptr_t)t234;
                    uint64_t t236 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                    uint64_t t237 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t235 * 8));
                    uint64_t t240 = GM_LD(uint64_t, ((uint8_t *)&t237));
                    gm_l35 = t240;
                    {
                        void * t243 = (void *)((uint8_t *)&F.l32);
                        uint64_t t244 = gm_l35;
                        uint64_t t245 = (uintptr_t)t244;
                        uint64_t t246 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
                        if (t245 >= (uintptr_t)(t246)) gm_trap_index(t245, t246, gm_srccfhjr4r9mdef, 689, 21);
                        gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l0)) + t245 * 56) + 0));
                        gm_ttsm01t0d8srg t248 = gm_f_get_ece4opn(t243, t247);
                        uint8_t t249;
                        if (((((uint8_t *)&t248))[0] != 0)) goto mn251;
                        t249 = ((uint8_t)0);
                        goto me250;
mn251:;
                        t249 = ((uint8_t)1);
                        goto me250;
mn252:;
                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 689, 12);
me250:;
                        if (!t249) goto ie242;
                        {
                            goto lc228;
                        }
                        goto ix242;
ie242:;
ix242:;
                    }
                    {
                        void * t254 = (void *)((uint8_t *)&F.l32);
                        uint64_t t255 = gm_l35;
                        uint64_t t256 = (uintptr_t)t255;
                        uint64_t t257 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
                        if (t256 >= (uintptr_t)(t257)) gm_trap_index(t256, t257, gm_srccfhjr4r9mdef, 690, 18);
                        gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l0)) + t256 * 56) + 0));
                        gm_b0_1 t259;
                        gm_tftb1odc739t5 t260 = gm_f_put_drsjd1t(t254, t258, t259);
                        if (((((uint8_t *)&t260))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t260) + 1, gm_srccfhjr4r9mdef, 690, 9);
                        gm_b0_1 t261 = GM_LD(gm_b0_1, (((uint8_t *)&t260) + 1));
                    }
                    {
                        void * t262 = (void *)((uint8_t *)&F.l31);
                        uint64_t t263 = gm_l35;
                        uint64_t t264 = (uintptr_t)t263;
                        uint64_t t265 = GM_LD(uintptr_t, ((uint8_t *)&F.l0) + 8);
                        if (t264 >= (uintptr_t)(t265)) gm_trap_index(t264, t265, gm_srccfhjr4r9mdef, 691, 21);
                        gm_b16_8 t266 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l0)) + t264 * 56) + 0));
                        gm_tftb1odc739t5 t267 = gm_f_extend_0cs85gs(t262, t266);
                        if (((((uint8_t *)&t267))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t267) + 1, gm_srccfhjr4r9mdef, 691, 9);
                        gm_b0_1 t268 = GM_LD(gm_b0_1, (((uint8_t *)&t267) + 1));
                    }
                    goto me238;
mn239:;
                    gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 687, 5);
me238:;
                }
lc228:;
                {
                    uint64_t t270 = gm_l34;
                    uint64_t r271; bool o272 = __builtin_add_overflow(t270, ((uint64_t)1ULL), &r271);
                    uint64_t t273 = r271;
                    gm_l34 = t273;
                }
            }
lb228:;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l36, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
        uint64_t t275 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t276 = (t275 > ((uint64_t)0ULL));
        if (!t276) goto ie274;
        {
            gm_b16_8 t278 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
            gm_b16_8 t279 = GM_LD(gm_b16_8, ((uint8_t *)&t278));
            gm_f_ident_set_9ajalpn((void *)&F.l37, (void *)NULL, cx, NULL, t279);
            if (gm_unw.kind) {
                goto gm_ld1_2;
            }
        }
        {
            {
                gm_b16_8 t281 = gm_l4;
                gm_l38 = t281;
            }
            {
                gm_l39 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t283 = gm_l39;
                    uint64_t t284 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                    uint8_t t285 = (t283 < t284);
                    if (!t285) goto lb282;
                    {
                        uint64_t t288 = gm_l39;
                        uint64_t t289 = (uintptr_t)t288;
                        uint64_t t290 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                        gm_b16_8 t293 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t289 * 16));
                        gm_l40 = t293;
                        {
                            void * t296 = (void *)((uint8_t *)&F.l37);
                            gm_b16_8 t297 = gm_l40;
                            gm_ttsm01t0d8srg t298 = gm_f_get_ece4opn(t296, t297);
                            uint8_t t299;
                            if (((((uint8_t *)&t298))[0] != 0)) goto mn301;
                            t299 = ((uint8_t)0);
                            goto me300;
mn301:;
                            t299 = ((uint8_t)1);
                            goto me300;
mn302:;
                            gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 696, 31);
me300:;
                            if (!t299) goto ie295;
                            {
                                void * t304 = (void *)((uint8_t *)&F.l36);
                                gm_b16_8 t305 = gm_l40;
                                gm_tftb1odc739t5 t306 = gm_f_push_3ef746b(t304, t305);
                                if (((((uint8_t *)&t306))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t306) + 1, gm_srccfhjr4r9mdef, 696, 59);
                                gm_b0_1 t307 = GM_LD(gm_b0_1, (((uint8_t *)&t306) + 1));
                            }
                            goto ix295;
ie295:;
ix295:;
                        }
                        goto me291;
mn292:;
                        gm_trap("no match arm matched the scrutinee", gm_srccfhjr4r9mdef, 696, 9);
me291:;
                    }
lc282:;
                    {
                        uint64_t t309 = gm_l39;
                        uint64_t r310; bool o311 = __builtin_add_overflow(t309, ((uint64_t)1ULL), &r310);
                        uint64_t t312 = r310;
                        gm_l39 = t312;
                    }
                }
lb282:;
            }
        }
        goto ix274;
ie274:;
ix274:;
    }
    {
        gm_t6j6smj7qoctu t313;
        gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 0));
        gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&t314));
        gm_b16_8 t316 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
        gm_b16_8 t317 = GM_LD(gm_b16_8, ((uint8_t *)&t316));
        gm_b16_8 t318 = gm_f_guard_o7k9a0e((void *)NULL, cx, NULL, t315, t317);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t313 + 0, t318);
        gm_b16_8 t319 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t313 + 16, t319);
        gm_tabb710d9mq2e t320;
        gm_f_format_8n626pi((void *)&t320, (void *)NULL, cx, NULL, &t313);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
        if (((((uint8_t *)&t320))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t320) + 8, gm_srccfhjr4r9mdef, 698, 12);
        gm_b16_8 t321 = GM_LD(gm_b16_8, (((uint8_t *)&t320) + 8));
        gm_b16_8 t322 = GM_LD(gm_b16_8, ((uint8_t *)&t321));
        uint8_t t323 = 0;
        F.ret = t322;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t323;
        goto gm_ld1_2;
    }
    {
        void * t324 = (void *)((uint8_t *)&F.l32);
        gm_f_deinit_lehld1n(t324);
    }
    {
        void * t325 = (void *)((uint8_t *)&F.l8);
        gm_f_deinit_lehld1n(t325);
    }
    goto gm_ls326;
gm_ld1_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t327 = (void *)((uint8_t *)&F.l32);
        gm_f_deinit_lehld1n(t327);
        gm_unw = gm_saved;
    }
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t328 = (void *)((uint8_t *)&F.l8);
        gm_f_deinit_lehld1n(t328);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls326:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
