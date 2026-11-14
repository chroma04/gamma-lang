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
typedef struct { uint64_t a[64]; } gm_t67js5mncp819;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t67js5mncp819) == 512 && _Alignof(gm_t67js5mncp819) == 8 && offsetof(gm_t67js5mncp819, a) == 0, "layout of `[64]usize`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
struct gm_fr_dedup_6a2r2j0 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_heap_sort_66f3gdb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_heap_sort_6hv5ha6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_insertion_sort_mgfcldh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_introsort_6k4m7ds {
    gm_frame h;
    gm_t67js5mncp819 l4; /* los */
    gm_t67js5mncp819 l5; /* his */
    gm_t67js5mncp819 l6; /* depths */
    gm_b0_1 ret;
};
struct gm_fr_pivot_partition_691mfjq {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_pivot_partition_69cutf4 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_pivot_partition_m75nekr {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_sift_down_6gb1v1d {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sort_by_m9qke1u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_swap_mehak34 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_swap_mo06hda {
    gm_frame h;
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint64_t gm_f_dedup_6a2r2j0(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_heap_sort_38cquor(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_heap_sort_66f3gdb(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_heap_sort_6hv5ha6(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_insertion_sort_84ls2i9(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_insertion_sort_mgfcldh(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_1n5fb1t(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_6k4m7ds(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint64_t gm_f_pivot_partition_691mfjq(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint64_t gm_f_pivot_partition_69cutf4(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint64_t gm_f_pivot_partition_i0hmj31(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint64_t gm_f_pivot_partition_m75nekr(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_sift_down_6gb1v1d(gm_b16_8 a0, uint64_t a1, uint64_t a2, gm_closure a3);
__attribute__((aligned(4))) void gm_f_sift_down_eg4c75v(gm_b16_8 a0, uint64_t a1, uint64_t a2, gm_closure a3);
__attribute__((aligned(4))) void gm_f_sift_down_h44o0bt(gm_b16_8 a0, uint64_t a1, uint64_t a2, gm_closure a3);
inline __attribute__((aligned(4))) void gm_f_sort_by_m9qke1u(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_swap_0g1sumg(gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_swap_2ogc2g8(gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_swap_8reg2vp(gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_swap_mehak34(gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_swap_mo06hda(gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_swap_qtnkug3(gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_swap_ssvhioc(gm_b16_8 a0, uint64_t a1, uint64_t a2);
extern const char gm_srcc24pnihhgkug[];
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) void gm_f_sort_by_m9qke1u(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_m9qke1u F;
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
        gm_f_introsort_1n5fb1t(t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_dedup_6a2r2j0(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_dedup_6a2r2j0 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 317u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_l1 = ((uint64_t)1ULL);
    }
    {
        {
            gm_l3 = ((uint64_t)1ULL);
        }
        {
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l2 = t8;
        }
        {
            for (;;) {
                uint64_t t10 = gm_l3;
                uint64_t t11 = gm_l2;
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l3;
                    uint64_t t16 = t15;
                    uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t16));
                    gm_l4 = t19;
                    {
                        uint64_t t22 = gm_l4;
                        uint64_t t23 = (uintptr_t)t22;
                        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcc24pnihhgkug, 365, 12);
                        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t23 * 16));
                        uint64_t t26 = gm_l1;
                        uint64_t r27; bool o28 = __builtin_sub_overflow(t26, ((uint64_t)1ULL), &r27);
                        if (o28) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 365, 26);
                        uint64_t t29 = r27;
                        uint64_t t30 = (uintptr_t)t29;
                        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcc24pnihhgkug, 365, 24);
                        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t30 * 16));
                        gm_b16_8 t33 = t25;
                        gm_b16_8 t34 = t32;
                        uint8_t t35 = (!gm_eq0iqiubfm7e(((uint8_t *)&t33), ((uint8_t *)&t34)));
                        if (!t35) goto ie21;
                        {
                            gm_b16_8 t37 = gm_l0;
                            uint64_t t38 = gm_l1;
                            uint64_t t39 = gm_l4;
                            gm_f_swap_0g1sumg(t37, t38, t39);
                        }
                        {
                            uint64_t t40 = gm_l1;
                            uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                            if (o42) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 367, 13);
                            uint64_t t43 = r41;
                            gm_l1 = t43;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 364, 5);
me17:;
                }
lc9:;
                {
                    uint64_t t45 = gm_l3;
                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                    uint64_t t48 = r46;
                    gm_l3 = t48;
                }
            }
lb9:;
        }
    }
    {
        uint64_t t49 = gm_l1;
        uint8_t t50 = 0;
        F.ret = t49;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_heap_sort_66f3gdb(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_heap_sort_66f3gdb F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* i */
    uint64_t gm_l4; /* end */
    GM_STACK_CHECK(&F, 274u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l2 = t2;
    }
    {
        uint64_t t3 = gm_l2;
        if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcc24pnihhgkug, 96, 10);
        uint64_t t4 = (uint64_t)(t3 / ((uint64_t)2ULL));
        gm_l3 = t4;
    }
    {
        for (;;) {
            uint64_t t6 = gm_l3;
            uint8_t t7 = (t6 > ((uint64_t)0ULL));
            if (!t7) goto lb5;
            {
                uint64_t t10 = gm_l3;
                uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
                if (o12) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 98, 9);
                uint64_t t13 = r11;
                gm_l3 = t13;
            }
            {
                gm_b16_8 t14 = gm_l0;
                uint64_t t15 = gm_l3;
                uint64_t t16 = gm_l2;
                gm_closure t17 = gm_l1;
                gm_f_sift_down_h44o0bt(t14, t15, t16, t17);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
            }
lc5:;
        }
gm_ll8:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb5; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc5; }
        }
        goto gm_lad;
lb5:;
    }
    {
        uint64_t t18 = gm_l2;
        gm_l4 = t18;
    }
    {
        for (;;) {
            uint64_t t20 = gm_l4;
            uint8_t t21 = (t20 > ((uint64_t)1ULL));
            if (!t21) goto lb19;
            {
                uint64_t t24 = gm_l4;
                uint64_t r25; bool o26 = __builtin_sub_overflow(t24, ((uint64_t)1ULL), &r25);
                if (o26) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 103, 9);
                uint64_t t27 = r25;
                gm_l4 = t27;
            }
            {
                gm_b16_8 t28 = gm_l0;
                uint64_t t29 = gm_l4;
                gm_f_swap_ssvhioc(t28, ((uint64_t)0ULL), t29);
            }
            {
                gm_b16_8 t30 = gm_l0;
                uint64_t t31 = gm_l4;
                gm_closure t32 = gm_l1;
                gm_f_sift_down_h44o0bt(t30, ((uint64_t)0ULL), t31, t32);
                if (gm_unw.kind) {
                    goto gm_ll22;
                }
            }
lc19:;
        }
gm_ll22:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
        }
        goto gm_lad;
lb19:;
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
inline __attribute__((aligned(4))) void gm_f_heap_sort_6hv5ha6(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_heap_sort_6hv5ha6 F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* i */
    uint64_t gm_l4; /* end */
    GM_STACK_CHECK(&F, 274u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l2 = t2;
    }
    {
        uint64_t t3 = gm_l2;
        if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcc24pnihhgkug, 96, 10);
        uint64_t t4 = (uint64_t)(t3 / ((uint64_t)2ULL));
        gm_l3 = t4;
    }
    {
        for (;;) {
            uint64_t t6 = gm_l3;
            uint8_t t7 = (t6 > ((uint64_t)0ULL));
            if (!t7) goto lb5;
            {
                uint64_t t10 = gm_l3;
                uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
                if (o12) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 98, 9);
                uint64_t t13 = r11;
                gm_l3 = t13;
            }
            {
                gm_b16_8 t14 = gm_l0;
                uint64_t t15 = gm_l3;
                uint64_t t16 = gm_l2;
                gm_closure t17 = gm_l1;
                gm_f_sift_down_eg4c75v(t14, t15, t16, t17);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
            }
lc5:;
        }
gm_ll8:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb5; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc5; }
        }
        goto gm_lad;
lb5:;
    }
    {
        uint64_t t18 = gm_l2;
        gm_l4 = t18;
    }
    {
        for (;;) {
            uint64_t t20 = gm_l4;
            uint8_t t21 = (t20 > ((uint64_t)1ULL));
            if (!t21) goto lb19;
            {
                uint64_t t24 = gm_l4;
                uint64_t r25; bool o26 = __builtin_sub_overflow(t24, ((uint64_t)1ULL), &r25);
                if (o26) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 103, 9);
                uint64_t t27 = r25;
                gm_l4 = t27;
            }
            {
                gm_b16_8 t28 = gm_l0;
                uint64_t t29 = gm_l4;
                gm_f_swap_0g1sumg(t28, ((uint64_t)0ULL), t29);
            }
            {
                gm_b16_8 t30 = gm_l0;
                uint64_t t31 = gm_l4;
                gm_closure t32 = gm_l1;
                gm_f_sift_down_eg4c75v(t30, ((uint64_t)0ULL), t31, t32);
                if (gm_unw.kind) {
                    goto gm_ll22;
                }
            }
lc19:;
        }
gm_ll22:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
        }
        goto gm_lad;
lb19:;
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
inline __attribute__((aligned(4))) void gm_f_insertion_sort_mgfcldh(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insertion_sort_mgfcldh F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    uint64_t gm_l2; /* i */
    gm_t3877pk7lv748 gm_l3; /* t */
    uint64_t gm_l4; /* j */
    GM_STACK_CHECK(&F, 427u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)1ULL);
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
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srcc24pnihhgkug, 69, 14);
                gm_t3877pk7lv748 t11 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 24));
                gm_l3 = t11;
            }
            {
                uint64_t t12 = gm_l2;
                gm_l4 = t12;
            }
            {
                for (;;) {
                    uint64_t t14 = gm_l4;
                    uint8_t t15 = (t14 > ((uint64_t)0ULL));
                    if (!t15) goto lb13;
                    gm_closure t16 = gm_l1;
                    void * t17 = (void *)((uint8_t *)&gm_l3);
                    uint64_t t18 = gm_l4;
                    uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
                    if (o20) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 71, 37);
                    uint64_t t21 = r19;
                    uint64_t t22 = (uintptr_t)t21;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_srcc24pnihhgkug, 71, 35);
                    void * t24 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t22 * 24);
                    gm_closure t25 = t16;
                    gm_node *ccx26 = t25.cx;
                    void * t27 = t25.code;
                    uint8_t t28;
                    if (GM_PTR_TAGGED(t27)) {
                        t28 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t27))(t17, t24);
                    } else {
                        t28 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t27)(t25.env, ccx26, NULL, t17, t24);
                    }
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                    if (!t28) goto lb13;
                    {
                        uint64_t t31 = gm_l4;
                        uint64_t t32 = (uintptr_t)t31;
                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcc24pnihhgkug, 72, 13);
                        uint64_t t34 = gm_l4;
                        uint64_t r35; bool o36 = __builtin_sub_overflow(t34, ((uint64_t)1ULL), &r35);
                        if (o36) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 72, 22);
                        uint64_t t37 = r35;
                        uint64_t t38 = (uintptr_t)t37;
                        uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srcc24pnihhgkug, 72, 20);
                        gm_t3877pk7lv748 t40 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t38 * 24));
                        GM_ST(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t32 * 24), t40);
                    }
                    {
                        uint64_t t41 = gm_l4;
                        uint64_t r42; bool o43 = __builtin_sub_overflow(t41, ((uint64_t)1ULL), &r42);
                        if (o43) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 73, 13);
                        uint64_t t44 = r42;
                        gm_l4 = t44;
                    }
lc13:;
                }
lb13:;
            }
            {
                uint64_t t45 = gm_l4;
                uint64_t t46 = (uintptr_t)t45;
                uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t46 >= (uintptr_t)(t47)) gm_trap_index(t46, t47, gm_srcc24pnihhgkug, 75, 9);
                gm_t3877pk7lv748 t48 = gm_l3;
                GM_ST(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t46 * 24), t48);
            }
            {
                uint64_t t49 = gm_l2;
                uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                if (o51) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 76, 9);
                uint64_t t52 = r50;
                gm_l2 = t52;
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
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_introsort_6k4m7ds(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_introsort_6k4m7ds F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    uint64_t gm_l2; /* depth */
    uint64_t gm_l3; /* m */
    uint64_t gm_l7; /* n */
    uint64_t gm_l8; /* lo */
    uint64_t gm_l9; /* hi */
    uint64_t gm_l10; /* p */
    GM_STACK_CHECK(&F, 805u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l3 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l3;
            uint8_t t5 = (t4 > ((uint64_t)1ULL));
            if (!t5) goto lb3;
            {
                uint64_t t8 = gm_l3;
                if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcc24pnihhgkug, 145, 9);
                uint64_t t9 = (uint64_t)(t8 / ((uint64_t)2ULL));
                gm_l3 = t9;
            }
            {
                uint64_t t10 = gm_l2;
                uint64_t r11; bool o12 = __builtin_add_overflow(t10, ((uint64_t)2ULL), &r11);
                if (o12) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 146, 9);
                uint64_t t13 = r11;
                gm_l2 = t13;
            }
lc3:;
        }
lb3:;
    }
    {
    }
    {
    }
    {
    }
    {
        gm_l7 = ((uint64_t)0ULL);
    }
    {
        gm_l8 = ((uint64_t)0ULL);
    }
    {
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l9 = t14;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb15;
            {
                uint64_t t19 = gm_l9;
                uint64_t t20 = gm_l8;
                uint64_t r21; bool o22 = __builtin_sub_overflow(t19, t20, &r21);
                if (o22) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 155, 12);
                uint64_t t23 = r21;
                uint8_t t24 = (t23 <= ((uint64_t)16ULL));
                if (!t24) goto ie18;
                {
                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t27 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t28 = gm_l8;
                    uint64_t t29 = (uintptr_t)t28;
                    uint64_t t30 = gm_l9;
                    uint64_t t31 = (uintptr_t)t30;
                    if (t29 > t31 || t31 > (uintptr_t)t26) gm_trap_slice(t29, t31, t26, gm_srcc24pnihhgkug, 156, 28);
                    gm_b16_8 t32;
                    GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t27 + t29 * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t29));
                    gm_closure t33 = gm_l1;
                    gm_f_insertion_sort_84ls2i9(t32, t33);
                    if (gm_unw.kind) {
                        goto gm_ll16;
                    }
                }
                goto ix18;
ie18:;
                uint64_t t35 = gm_l2;
                uint8_t t36 = ((t35 == ((uint64_t)0ULL)));
                if (!t36) goto ie34;
                {
                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t39 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t40 = gm_l8;
                    uint64_t t41 = (uintptr_t)t40;
                    uint64_t t42 = gm_l9;
                    uint64_t t43 = (uintptr_t)t42;
                    if (t41 > t43 || t43 > (uintptr_t)t38) gm_trap_slice(t41, t43, t38, gm_srcc24pnihhgkug, 158, 23);
                    gm_b16_8 t44;
                    GM_ST(void *, (uint8_t *)&t44, (void *)(((uint8_t *)t39 + t41 * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(t43 - t41));
                    gm_closure t45 = gm_l1;
                    gm_f_heap_sort_38cquor(t44, t45);
                    if (gm_unw.kind) {
                        goto gm_ll16;
                    }
                }
                goto ix34;
ie34:;
                {
                    uint64_t t47 = gm_l8;
                    uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t49 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t50 = gm_l8;
                    uint64_t t51 = (uintptr_t)t50;
                    uint64_t t52 = gm_l9;
                    uint64_t t53 = (uintptr_t)t52;
                    if (t51 > t53 || t53 > (uintptr_t)t48) gm_trap_slice(t51, t53, t48, gm_srcc24pnihhgkug, 160, 39);
                    gm_b16_8 t54;
                    GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t49 + t51 * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53 - t51));
                    gm_closure t55 = gm_l1;
                    uint64_t t56 = gm_f_pivot_partition_i0hmj31(t54, t55);
                    if (gm_unw.kind) {
                        goto gm_ll16;
                    }
                    uint64_t r57; bool o58 = __builtin_add_overflow(t47, t56, &r57);
                    if (o58) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 160, 18);
                    uint64_t t59 = r57;
                    gm_l10 = t59;
                }
                {
                    uint64_t t60 = gm_l2;
                    uint64_t r61; bool o62 = __builtin_sub_overflow(t60, ((uint64_t)1ULL), &r61);
                    if (o62) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 161, 13);
                    uint64_t t63 = r61;
                    gm_l2 = t63;
                }
                {
                    uint64_t t65 = gm_l10;
                    uint64_t t66 = gm_l8;
                    uint64_t r67; bool o68 = __builtin_sub_overflow(t65, t66, &r67);
                    if (o68) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 162, 16);
                    uint64_t t69 = r67;
                    uint64_t t70 = gm_l9;
                    uint64_t t71 = gm_l10;
                    uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                    if (o73) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 162, 31);
                    uint64_t t74 = r72;
                    uint64_t r75; bool o76 = __builtin_sub_overflow(t70, t74, &r75);
                    if (o76) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 162, 25);
                    uint64_t t77 = r75;
                    uint8_t t78 = (t69 < t77);
                    if (!t78) goto ie64;
                    {
                        uint64_t t80 = gm_l7;
                        uint64_t t81 = (uintptr_t)t80;
                        if (t81 >= (uintptr_t)(64)) gm_trap_index(t81, 64, gm_srcc24pnihhgkug, 163, 17);
                        uint64_t t82 = gm_l10;
                        uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                        if (o84) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 163, 26);
                        uint64_t t85 = r83;
                        GM_ST(uint64_t, (((uint8_t *)&F.l4) + t81 * 8), t85);
                    }
                    {
                        uint64_t t86 = gm_l7;
                        uint64_t t87 = (uintptr_t)t86;
                        if (t87 >= (uintptr_t)(64)) gm_trap_index(t87, 64, gm_srcc24pnihhgkug, 164, 17);
                        uint64_t t88 = gm_l9;
                        GM_ST(uint64_t, (((uint8_t *)&F.l5) + t87 * 8), t88);
                    }
                    {
                        uint64_t t89 = gm_l10;
                        gm_l9 = t89;
                    }
                    goto ix64;
ie64:;
                    {
                        uint64_t t91 = gm_l7;
                        uint64_t t92 = (uintptr_t)t91;
                        if (t92 >= (uintptr_t)(64)) gm_trap_index(t92, 64, gm_srcc24pnihhgkug, 167, 17);
                        uint64_t t93 = gm_l8;
                        GM_ST(uint64_t, (((uint8_t *)&F.l4) + t92 * 8), t93);
                    }
                    {
                        uint64_t t94 = gm_l7;
                        uint64_t t95 = (uintptr_t)t94;
                        if (t95 >= (uintptr_t)(64)) gm_trap_index(t95, 64, gm_srcc24pnihhgkug, 168, 17);
                        uint64_t t96 = gm_l10;
                        GM_ST(uint64_t, (((uint8_t *)&F.l5) + t95 * 8), t96);
                    }
                    {
                        uint64_t t97 = gm_l10;
                        uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                        if (o99) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 169, 22);
                        uint64_t t100 = r98;
                        gm_l8 = t100;
                    }
ix64:;
                }
                {
                    uint64_t t101 = gm_l7;
                    uint64_t t102 = (uintptr_t)t101;
                    if (t102 >= (uintptr_t)(64)) gm_trap_index(t102, 64, gm_srcc24pnihhgkug, 171, 13);
                    uint64_t t103 = gm_l2;
                    GM_ST(uint64_t, (((uint8_t *)&F.l6) + t102 * 8), t103);
                }
                {
                    uint64_t t104 = gm_l7;
                    uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                    if (o106) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 172, 13);
                    uint64_t t107 = r105;
                    gm_l7 = t107;
                }
                {
                    goto lc15;
                }
ix34:;
ix18:;
            }
            {
                uint64_t t109 = gm_l7;
                uint8_t t110 = ((t109 == ((uint64_t)0ULL)));
                if (!t110) goto ie108;
                {
                    goto lb15;
                }
                goto ix108;
ie108:;
ix108:;
            }
            {
                uint64_t t112 = gm_l7;
                uint64_t r113; bool o114 = __builtin_sub_overflow(t112, ((uint64_t)1ULL), &r113);
                if (o114) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 176, 9);
                uint64_t t115 = r113;
                gm_l7 = t115;
            }
            {
                uint64_t t116 = gm_l7;
                uint64_t t117 = (uintptr_t)t116;
                if (t117 >= (uintptr_t)(64)) gm_trap_index(t117, 64, gm_srcc24pnihhgkug, 177, 14);
                uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + t117 * 8));
                gm_l8 = t118;
            }
            {
                uint64_t t119 = gm_l7;
                uint64_t t120 = (uintptr_t)t119;
                if (t120 >= (uintptr_t)(64)) gm_trap_index(t120, 64, gm_srcc24pnihhgkug, 178, 14);
                uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + t120 * 8));
                gm_l9 = t121;
            }
            {
                uint64_t t122 = gm_l7;
                uint64_t t123 = (uintptr_t)t122;
                if (t123 >= (uintptr_t)(64)) gm_trap_index(t123, 64, gm_srcc24pnihhgkug, 179, 17);
                uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + t123 * 8));
                gm_l2 = t124;
            }
lc15:;
        }
gm_ll16:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb15; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc15; }
        }
        goto gm_lad;
lb15:;
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
__attribute__((aligned(4))) uint64_t gm_f_pivot_partition_691mfjq(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pivot_partition_691mfjq F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* a */
    uint64_t gm_l4; /* b */
    uint64_t gm_l5; /* c */
    uint64_t gm_l6; /* i */
    uint64_t gm_l7; /* j */
    GM_STACK_CHECK(&F, 1073u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l2 = t2;
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        uint64_t t3 = gm_l2;
        if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcc24pnihhgkug, 114, 10);
        uint64_t t4 = (uint64_t)(t3 / ((uint64_t)2ULL));
        gm_l4 = t4;
    }
    {
        uint64_t t5 = gm_l2;
        uint64_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint64_t)1ULL), &r6);
        if (o7) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 115, 10);
        uint64_t t8 = r6;
        gm_l5 = t8;
    }
    {
        gm_closure t10 = gm_l1;
        uint64_t t11 = gm_l4;
        uint64_t t12 = (uintptr_t)t11;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srcc24pnihhgkug, 116, 14);
        void * t14 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t12 * 16);
        uint64_t t15 = gm_l3;
        uint64_t t16 = (uintptr_t)t15;
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_srcc24pnihhgkug, 116, 21);
        void * t18 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t16 * 16);
        gm_closure t19 = t10;
        gm_node *ccx20 = t19.cx;
        void * t21 = t19.code;
        uint8_t t22;
        if (GM_PTR_TAGGED(t21)) {
            t22 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t21))(t14, t18);
        } else {
            t22 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t21)(t19.env, ccx20, NULL, t14, t18);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t22) goto ie9;
        {
            gm_b16_8 t24 = gm_l0;
            uint64_t t25 = gm_l3;
            uint64_t t26 = gm_l4;
            gm_f_swap_0g1sumg(t24, t25, t26);
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_closure t28 = gm_l1;
        uint64_t t29 = gm_l5;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcc24pnihhgkug, 117, 14);
        void * t32 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t30 * 16);
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_srcc24pnihhgkug, 117, 21);
        void * t36 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t34 * 16);
        gm_closure t37 = t28;
        gm_node *ccx38 = t37.cx;
        void * t39 = t37.code;
        uint8_t t40;
        if (GM_PTR_TAGGED(t39)) {
            t40 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t39))(t32, t36);
        } else {
            t40 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t39)(t37.env, ccx38, NULL, t32, t36);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t40) goto ie27;
        {
            gm_b16_8 t42 = gm_l0;
            uint64_t t43 = gm_l4;
            uint64_t t44 = gm_l5;
            gm_f_swap_0g1sumg(t42, t43, t44);
        }
        {
            gm_closure t46 = gm_l1;
            uint64_t t47 = gm_l4;
            uint64_t t48 = (uintptr_t)t47;
            uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t48 >= (uintptr_t)(t49)) gm_trap_index(t48, t49, gm_srcc24pnihhgkug, 119, 18);
            void * t50 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t48 * 16);
            uint64_t t51 = gm_l3;
            uint64_t t52 = (uintptr_t)t51;
            uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t52 >= (uintptr_t)(t53)) gm_trap_index(t52, t53, gm_srcc24pnihhgkug, 119, 25);
            void * t54 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t52 * 16);
            gm_closure t55 = t46;
            gm_node *ccx56 = t55.cx;
            void * t57 = t55.code;
            uint8_t t58;
            if (GM_PTR_TAGGED(t57)) {
                t58 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t57))(t50, t54);
            } else {
                t58 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t57)(t55.env, ccx56, NULL, t50, t54);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t58) goto ie45;
            {
                gm_b16_8 t60 = gm_l0;
                uint64_t t61 = gm_l3;
                uint64_t t62 = gm_l4;
                gm_f_swap_0g1sumg(t60, t61, t62);
            }
            goto ix45;
ie45:;
ix45:;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        gm_b16_8 t63 = gm_l0;
        uint64_t t64 = gm_l4;
        gm_f_swap_0g1sumg(t63, ((uint64_t)0ULL), t64);
    }
    {
        gm_l6 = ((uint64_t)1ULL);
    }
    {
        uint64_t t65 = gm_l2;
        gm_l7 = t65;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb66;
            {
                for (;;) {
                    uint64_t t70 = gm_l6;
                    uint64_t t71 = gm_l7;
                    uint8_t t72 = (t70 < t71);
                    if (!t72) goto lb69;
                    gm_closure t73 = gm_l1;
                    uint64_t t74 = gm_l6;
                    uint64_t t75 = (uintptr_t)t74;
                    uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t75 >= (uintptr_t)(t76)) gm_trap_index(t75, t76, gm_srcc24pnihhgkug, 126, 31);
                    void * t77 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t75 * 16);
                    uint64_t t78 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_srcc24pnihhgkug, 126, 38);
                    void * t80 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t78 * 16);
                    gm_closure t81 = t73;
                    gm_node *ccx82 = t81.cx;
                    void * t83 = t81.code;
                    uint8_t t84;
                    if (GM_PTR_TAGGED(t83)) {
                        t84 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t83))(t77, t80);
                    } else {
                        t84 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t83)(t81.env, ccx82, NULL, t77, t80);
                    }
                    if (gm_unw.kind) {
                        goto gm_ll67;
                    }
                    if (!t84) goto lb69;
                    {
                        uint64_t t87 = gm_l6;
                        uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                        if (o89) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 126, 46);
                        uint64_t t90 = r88;
                        gm_l6 = t90;
                    }
lc69:;
                }
lb69:;
            }
            {
                for (;;) {
                    uint64_t t92 = gm_l6;
                    uint64_t t93 = gm_l7;
                    uint8_t t94 = (t92 < t93);
                    if (!t94) goto lb91;
                    gm_closure t95 = gm_l1;
                    uint64_t t96 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t96 >= (uintptr_t)(t97)) gm_trap_index(t96, t97, gm_srcc24pnihhgkug, 127, 31);
                    void * t98 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t96 * 16);
                    uint64_t t99 = gm_l7;
                    uint64_t r100; bool o101 = __builtin_sub_overflow(t99, ((uint64_t)1ULL), &r100);
                    if (o101) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 127, 40);
                    uint64_t t102 = r100;
                    uint64_t t103 = (uintptr_t)t102;
                    uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t103 >= (uintptr_t)(t104)) gm_trap_index(t103, t104, gm_srcc24pnihhgkug, 127, 38);
                    void * t105 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t103 * 16);
                    gm_closure t106 = t95;
                    gm_node *ccx107 = t106.cx;
                    void * t108 = t106.code;
                    uint8_t t109;
                    if (GM_PTR_TAGGED(t108)) {
                        t109 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t108))(t98, t105);
                    } else {
                        t109 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t108)(t106.env, ccx107, NULL, t98, t105);
                    }
                    if (gm_unw.kind) {
                        goto gm_ll67;
                    }
                    if (!t109) goto lb91;
                    {
                        uint64_t t112 = gm_l7;
                        uint64_t r113; bool o114 = __builtin_sub_overflow(t112, ((uint64_t)1ULL), &r113);
                        if (o114) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 127, 50);
                        uint64_t t115 = r113;
                        gm_l7 = t115;
                    }
lc91:;
                }
lb91:;
            }
            {
                uint64_t t117 = gm_l6;
                uint64_t t118 = gm_l7;
                uint8_t t119 = (t117 >= t118);
                if (!t119) goto ie116;
                {
                    goto lb66;
                }
                goto ix116;
ie116:;
ix116:;
            }
            {
                gm_b16_8 t121 = gm_l0;
                uint64_t t122 = gm_l6;
                uint64_t t123 = gm_l7;
                uint64_t r124; bool o125 = __builtin_sub_overflow(t123, ((uint64_t)1ULL), &r124);
                if (o125) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 129, 19);
                uint64_t t126 = r124;
                gm_f_swap_0g1sumg(t121, t122, t126);
            }
            {
                uint64_t t127 = gm_l6;
                uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                if (o129) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 130, 9);
                uint64_t t130 = r128;
                gm_l6 = t130;
            }
            {
                uint64_t t131 = gm_l7;
                uint64_t r132; bool o133 = __builtin_sub_overflow(t131, ((uint64_t)1ULL), &r132);
                if (o133) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 131, 9);
                uint64_t t134 = r132;
                gm_l7 = t134;
            }
lc66:;
        }
gm_ll67:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb66; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc66; }
        }
        goto gm_lad;
lb66:;
    }
    {
        gm_b16_8 t135 = gm_l0;
        uint64_t t136 = gm_l6;
        uint64_t r137; bool o138 = __builtin_sub_overflow(t136, ((uint64_t)1ULL), &r137);
        if (o138) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 133, 15);
        uint64_t t139 = r137;
        gm_f_swap_0g1sumg(t135, ((uint64_t)0ULL), t139);
    }
    {
        uint64_t t140 = gm_l6;
        uint64_t r141; bool o142 = __builtin_sub_overflow(t140, ((uint64_t)1ULL), &r141);
        if (o142) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 134, 12);
        uint64_t t143 = r141;
        uint8_t t144 = 0;
        F.ret = t143;
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
__attribute__((aligned(4))) uint64_t gm_f_pivot_partition_69cutf4(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pivot_partition_69cutf4 F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* a */
    uint64_t gm_l4; /* b */
    uint64_t gm_l5; /* c */
    uint64_t gm_l6; /* i */
    uint64_t gm_l7; /* j */
    GM_STACK_CHECK(&F, 1073u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l2 = t2;
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        uint64_t t3 = gm_l2;
        if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcc24pnihhgkug, 114, 10);
        uint64_t t4 = (uint64_t)(t3 / ((uint64_t)2ULL));
        gm_l4 = t4;
    }
    {
        uint64_t t5 = gm_l2;
        uint64_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint64_t)1ULL), &r6);
        if (o7) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 115, 10);
        uint64_t t8 = r6;
        gm_l5 = t8;
    }
    {
        gm_closure t10 = gm_l1;
        uint64_t t11 = gm_l4;
        uint64_t t12 = (uintptr_t)t11;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srcc24pnihhgkug, 116, 14);
        void * t14 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t12 * 16);
        uint64_t t15 = gm_l3;
        uint64_t t16 = (uintptr_t)t15;
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_srcc24pnihhgkug, 116, 21);
        void * t18 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t16 * 16);
        gm_closure t19 = t10;
        gm_node *ccx20 = t19.cx;
        void * t21 = t19.code;
        uint8_t t22;
        if (GM_PTR_TAGGED(t21)) {
            t22 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t21))(t14, t18);
        } else {
            t22 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t21)(t19.env, ccx20, NULL, t14, t18);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t22) goto ie9;
        {
            gm_b16_8 t24 = gm_l0;
            uint64_t t25 = gm_l3;
            uint64_t t26 = gm_l4;
            gm_f_swap_8reg2vp(t24, t25, t26);
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_closure t28 = gm_l1;
        uint64_t t29 = gm_l5;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcc24pnihhgkug, 117, 14);
        void * t32 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t30 * 16);
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_srcc24pnihhgkug, 117, 21);
        void * t36 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t34 * 16);
        gm_closure t37 = t28;
        gm_node *ccx38 = t37.cx;
        void * t39 = t37.code;
        uint8_t t40;
        if (GM_PTR_TAGGED(t39)) {
            t40 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t39))(t32, t36);
        } else {
            t40 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t39)(t37.env, ccx38, NULL, t32, t36);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t40) goto ie27;
        {
            gm_b16_8 t42 = gm_l0;
            uint64_t t43 = gm_l4;
            uint64_t t44 = gm_l5;
            gm_f_swap_8reg2vp(t42, t43, t44);
        }
        {
            gm_closure t46 = gm_l1;
            uint64_t t47 = gm_l4;
            uint64_t t48 = (uintptr_t)t47;
            uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t48 >= (uintptr_t)(t49)) gm_trap_index(t48, t49, gm_srcc24pnihhgkug, 119, 18);
            void * t50 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t48 * 16);
            uint64_t t51 = gm_l3;
            uint64_t t52 = (uintptr_t)t51;
            uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t52 >= (uintptr_t)(t53)) gm_trap_index(t52, t53, gm_srcc24pnihhgkug, 119, 25);
            void * t54 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t52 * 16);
            gm_closure t55 = t46;
            gm_node *ccx56 = t55.cx;
            void * t57 = t55.code;
            uint8_t t58;
            if (GM_PTR_TAGGED(t57)) {
                t58 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t57))(t50, t54);
            } else {
                t58 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t57)(t55.env, ccx56, NULL, t50, t54);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t58) goto ie45;
            {
                gm_b16_8 t60 = gm_l0;
                uint64_t t61 = gm_l3;
                uint64_t t62 = gm_l4;
                gm_f_swap_8reg2vp(t60, t61, t62);
            }
            goto ix45;
ie45:;
ix45:;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        gm_b16_8 t63 = gm_l0;
        uint64_t t64 = gm_l4;
        gm_f_swap_8reg2vp(t63, ((uint64_t)0ULL), t64);
    }
    {
        gm_l6 = ((uint64_t)1ULL);
    }
    {
        uint64_t t65 = gm_l2;
        gm_l7 = t65;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb66;
            {
                for (;;) {
                    uint64_t t70 = gm_l6;
                    uint64_t t71 = gm_l7;
                    uint8_t t72 = (t70 < t71);
                    if (!t72) goto lb69;
                    gm_closure t73 = gm_l1;
                    uint64_t t74 = gm_l6;
                    uint64_t t75 = (uintptr_t)t74;
                    uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t75 >= (uintptr_t)(t76)) gm_trap_index(t75, t76, gm_srcc24pnihhgkug, 126, 31);
                    void * t77 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t75 * 16);
                    uint64_t t78 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_srcc24pnihhgkug, 126, 38);
                    void * t80 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t78 * 16);
                    gm_closure t81 = t73;
                    gm_node *ccx82 = t81.cx;
                    void * t83 = t81.code;
                    uint8_t t84;
                    if (GM_PTR_TAGGED(t83)) {
                        t84 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t83))(t77, t80);
                    } else {
                        t84 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t83)(t81.env, ccx82, NULL, t77, t80);
                    }
                    if (gm_unw.kind) {
                        goto gm_ll67;
                    }
                    if (!t84) goto lb69;
                    {
                        uint64_t t87 = gm_l6;
                        uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                        if (o89) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 126, 46);
                        uint64_t t90 = r88;
                        gm_l6 = t90;
                    }
lc69:;
                }
lb69:;
            }
            {
                for (;;) {
                    uint64_t t92 = gm_l6;
                    uint64_t t93 = gm_l7;
                    uint8_t t94 = (t92 < t93);
                    if (!t94) goto lb91;
                    gm_closure t95 = gm_l1;
                    uint64_t t96 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t96 >= (uintptr_t)(t97)) gm_trap_index(t96, t97, gm_srcc24pnihhgkug, 127, 31);
                    void * t98 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t96 * 16);
                    uint64_t t99 = gm_l7;
                    uint64_t r100; bool o101 = __builtin_sub_overflow(t99, ((uint64_t)1ULL), &r100);
                    if (o101) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 127, 40);
                    uint64_t t102 = r100;
                    uint64_t t103 = (uintptr_t)t102;
                    uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t103 >= (uintptr_t)(t104)) gm_trap_index(t103, t104, gm_srcc24pnihhgkug, 127, 38);
                    void * t105 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t103 * 16);
                    gm_closure t106 = t95;
                    gm_node *ccx107 = t106.cx;
                    void * t108 = t106.code;
                    uint8_t t109;
                    if (GM_PTR_TAGGED(t108)) {
                        t109 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t108))(t98, t105);
                    } else {
                        t109 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t108)(t106.env, ccx107, NULL, t98, t105);
                    }
                    if (gm_unw.kind) {
                        goto gm_ll67;
                    }
                    if (!t109) goto lb91;
                    {
                        uint64_t t112 = gm_l7;
                        uint64_t r113; bool o114 = __builtin_sub_overflow(t112, ((uint64_t)1ULL), &r113);
                        if (o114) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 127, 50);
                        uint64_t t115 = r113;
                        gm_l7 = t115;
                    }
lc91:;
                }
lb91:;
            }
            {
                uint64_t t117 = gm_l6;
                uint64_t t118 = gm_l7;
                uint8_t t119 = (t117 >= t118);
                if (!t119) goto ie116;
                {
                    goto lb66;
                }
                goto ix116;
ie116:;
ix116:;
            }
            {
                gm_b16_8 t121 = gm_l0;
                uint64_t t122 = gm_l6;
                uint64_t t123 = gm_l7;
                uint64_t r124; bool o125 = __builtin_sub_overflow(t123, ((uint64_t)1ULL), &r124);
                if (o125) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 129, 19);
                uint64_t t126 = r124;
                gm_f_swap_8reg2vp(t121, t122, t126);
            }
            {
                uint64_t t127 = gm_l6;
                uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                if (o129) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 130, 9);
                uint64_t t130 = r128;
                gm_l6 = t130;
            }
            {
                uint64_t t131 = gm_l7;
                uint64_t r132; bool o133 = __builtin_sub_overflow(t131, ((uint64_t)1ULL), &r132);
                if (o133) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 131, 9);
                uint64_t t134 = r132;
                gm_l7 = t134;
            }
lc66:;
        }
gm_ll67:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb66; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc66; }
        }
        goto gm_lad;
lb66:;
    }
    {
        gm_b16_8 t135 = gm_l0;
        uint64_t t136 = gm_l6;
        uint64_t r137; bool o138 = __builtin_sub_overflow(t136, ((uint64_t)1ULL), &r137);
        if (o138) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 133, 15);
        uint64_t t139 = r137;
        gm_f_swap_8reg2vp(t135, ((uint64_t)0ULL), t139);
    }
    {
        uint64_t t140 = gm_l6;
        uint64_t r141; bool o142 = __builtin_sub_overflow(t140, ((uint64_t)1ULL), &r141);
        if (o142) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 134, 12);
        uint64_t t143 = r141;
        uint8_t t144 = 0;
        F.ret = t143;
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
__attribute__((aligned(4))) uint64_t gm_f_pivot_partition_m75nekr(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pivot_partition_m75nekr F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* a */
    uint64_t gm_l4; /* b */
    uint64_t gm_l5; /* c */
    uint64_t gm_l6; /* i */
    uint64_t gm_l7; /* j */
    GM_STACK_CHECK(&F, 1073u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l2 = t2;
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        uint64_t t3 = gm_l2;
        if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcc24pnihhgkug, 114, 10);
        uint64_t t4 = (uint64_t)(t3 / ((uint64_t)2ULL));
        gm_l4 = t4;
    }
    {
        uint64_t t5 = gm_l2;
        uint64_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint64_t)1ULL), &r6);
        if (o7) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 115, 10);
        uint64_t t8 = r6;
        gm_l5 = t8;
    }
    {
        gm_closure t10 = gm_l1;
        uint64_t t11 = gm_l4;
        uint64_t t12 = (uintptr_t)t11;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srcc24pnihhgkug, 116, 14);
        void * t14 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t12 * 12);
        uint64_t t15 = gm_l3;
        uint64_t t16 = (uintptr_t)t15;
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_srcc24pnihhgkug, 116, 21);
        void * t18 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t16 * 12);
        gm_closure t19 = t10;
        gm_node *ccx20 = t19.cx;
        void * t21 = t19.code;
        uint8_t t22;
        if (GM_PTR_TAGGED(t21)) {
            t22 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t21))(t14, t18);
        } else {
            t22 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t21)(t19.env, ccx20, NULL, t14, t18);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t22) goto ie9;
        {
            gm_b16_8 t24 = gm_l0;
            uint64_t t25 = gm_l3;
            uint64_t t26 = gm_l4;
            gm_f_swap_qtnkug3(t24, t25, t26);
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_closure t28 = gm_l1;
        uint64_t t29 = gm_l5;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcc24pnihhgkug, 117, 14);
        void * t32 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t30 * 12);
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_srcc24pnihhgkug, 117, 21);
        void * t36 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t34 * 12);
        gm_closure t37 = t28;
        gm_node *ccx38 = t37.cx;
        void * t39 = t37.code;
        uint8_t t40;
        if (GM_PTR_TAGGED(t39)) {
            t40 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t39))(t32, t36);
        } else {
            t40 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t39)(t37.env, ccx38, NULL, t32, t36);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t40) goto ie27;
        {
            gm_b16_8 t42 = gm_l0;
            uint64_t t43 = gm_l4;
            uint64_t t44 = gm_l5;
            gm_f_swap_qtnkug3(t42, t43, t44);
        }
        {
            gm_closure t46 = gm_l1;
            uint64_t t47 = gm_l4;
            uint64_t t48 = (uintptr_t)t47;
            uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t48 >= (uintptr_t)(t49)) gm_trap_index(t48, t49, gm_srcc24pnihhgkug, 119, 18);
            void * t50 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t48 * 12);
            uint64_t t51 = gm_l3;
            uint64_t t52 = (uintptr_t)t51;
            uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t52 >= (uintptr_t)(t53)) gm_trap_index(t52, t53, gm_srcc24pnihhgkug, 119, 25);
            void * t54 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t52 * 12);
            gm_closure t55 = t46;
            gm_node *ccx56 = t55.cx;
            void * t57 = t55.code;
            uint8_t t58;
            if (GM_PTR_TAGGED(t57)) {
                t58 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t57))(t50, t54);
            } else {
                t58 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t57)(t55.env, ccx56, NULL, t50, t54);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t58) goto ie45;
            {
                gm_b16_8 t60 = gm_l0;
                uint64_t t61 = gm_l3;
                uint64_t t62 = gm_l4;
                gm_f_swap_qtnkug3(t60, t61, t62);
            }
            goto ix45;
ie45:;
ix45:;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        gm_b16_8 t63 = gm_l0;
        uint64_t t64 = gm_l4;
        gm_f_swap_qtnkug3(t63, ((uint64_t)0ULL), t64);
    }
    {
        gm_l6 = ((uint64_t)1ULL);
    }
    {
        uint64_t t65 = gm_l2;
        gm_l7 = t65;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb66;
            {
                for (;;) {
                    uint64_t t70 = gm_l6;
                    uint64_t t71 = gm_l7;
                    uint8_t t72 = (t70 < t71);
                    if (!t72) goto lb69;
                    gm_closure t73 = gm_l1;
                    uint64_t t74 = gm_l6;
                    uint64_t t75 = (uintptr_t)t74;
                    uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t75 >= (uintptr_t)(t76)) gm_trap_index(t75, t76, gm_srcc24pnihhgkug, 126, 31);
                    void * t77 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t75 * 12);
                    uint64_t t78 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_srcc24pnihhgkug, 126, 38);
                    void * t80 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t78 * 12);
                    gm_closure t81 = t73;
                    gm_node *ccx82 = t81.cx;
                    void * t83 = t81.code;
                    uint8_t t84;
                    if (GM_PTR_TAGGED(t83)) {
                        t84 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t83))(t77, t80);
                    } else {
                        t84 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t83)(t81.env, ccx82, NULL, t77, t80);
                    }
                    if (gm_unw.kind) {
                        goto gm_ll67;
                    }
                    if (!t84) goto lb69;
                    {
                        uint64_t t87 = gm_l6;
                        uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                        if (o89) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 126, 46);
                        uint64_t t90 = r88;
                        gm_l6 = t90;
                    }
lc69:;
                }
lb69:;
            }
            {
                for (;;) {
                    uint64_t t92 = gm_l6;
                    uint64_t t93 = gm_l7;
                    uint8_t t94 = (t92 < t93);
                    if (!t94) goto lb91;
                    gm_closure t95 = gm_l1;
                    uint64_t t96 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t96 >= (uintptr_t)(t97)) gm_trap_index(t96, t97, gm_srcc24pnihhgkug, 127, 31);
                    void * t98 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t96 * 12);
                    uint64_t t99 = gm_l7;
                    uint64_t r100; bool o101 = __builtin_sub_overflow(t99, ((uint64_t)1ULL), &r100);
                    if (o101) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 127, 40);
                    uint64_t t102 = r100;
                    uint64_t t103 = (uintptr_t)t102;
                    uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t103 >= (uintptr_t)(t104)) gm_trap_index(t103, t104, gm_srcc24pnihhgkug, 127, 38);
                    void * t105 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t103 * 12);
                    gm_closure t106 = t95;
                    gm_node *ccx107 = t106.cx;
                    void * t108 = t106.code;
                    uint8_t t109;
                    if (GM_PTR_TAGGED(t108)) {
                        t109 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t108))(t98, t105);
                    } else {
                        t109 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t108)(t106.env, ccx107, NULL, t98, t105);
                    }
                    if (gm_unw.kind) {
                        goto gm_ll67;
                    }
                    if (!t109) goto lb91;
                    {
                        uint64_t t112 = gm_l7;
                        uint64_t r113; bool o114 = __builtin_sub_overflow(t112, ((uint64_t)1ULL), &r113);
                        if (o114) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 127, 50);
                        uint64_t t115 = r113;
                        gm_l7 = t115;
                    }
lc91:;
                }
lb91:;
            }
            {
                uint64_t t117 = gm_l6;
                uint64_t t118 = gm_l7;
                uint8_t t119 = (t117 >= t118);
                if (!t119) goto ie116;
                {
                    goto lb66;
                }
                goto ix116;
ie116:;
ix116:;
            }
            {
                gm_b16_8 t121 = gm_l0;
                uint64_t t122 = gm_l6;
                uint64_t t123 = gm_l7;
                uint64_t r124; bool o125 = __builtin_sub_overflow(t123, ((uint64_t)1ULL), &r124);
                if (o125) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 129, 19);
                uint64_t t126 = r124;
                gm_f_swap_qtnkug3(t121, t122, t126);
            }
            {
                uint64_t t127 = gm_l6;
                uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                if (o129) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 130, 9);
                uint64_t t130 = r128;
                gm_l6 = t130;
            }
            {
                uint64_t t131 = gm_l7;
                uint64_t r132; bool o133 = __builtin_sub_overflow(t131, ((uint64_t)1ULL), &r132);
                if (o133) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 131, 9);
                uint64_t t134 = r132;
                gm_l7 = t134;
            }
lc66:;
        }
gm_ll67:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb66; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc66; }
        }
        goto gm_lad;
lb66:;
    }
    {
        gm_b16_8 t135 = gm_l0;
        uint64_t t136 = gm_l6;
        uint64_t r137; bool o138 = __builtin_sub_overflow(t136, ((uint64_t)1ULL), &r137);
        if (o138) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 133, 15);
        uint64_t t139 = r137;
        gm_f_swap_qtnkug3(t135, ((uint64_t)0ULL), t139);
    }
    {
        uint64_t t140 = gm_l6;
        uint64_t r141; bool o142 = __builtin_sub_overflow(t140, ((uint64_t)1ULL), &r141);
        if (o142) gm_trap("integer overflow in `-`", gm_srcc24pnihhgkug, 134, 12);
        uint64_t t143 = r141;
        uint8_t t144 = 0;
        F.ret = t143;
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
__attribute__((aligned(4))) void gm_f_sift_down_6gb1v1d(gm_b16_8 a0, uint64_t a1, uint64_t a2, gm_closure a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sift_down_6gb1v1d F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* root */
    uint64_t gm_l2; /* end */
    gm_closure gm_l3; /* less */
    uint64_t gm_l4; /* r */
    uint64_t gm_l5; /* child */
    GM_STACK_CHECK(&F, 453u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l1;
        gm_l4 = t2;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                uint64_t t6 = gm_l4;
                uint64_t r7; bool o8 = __builtin_mul_overflow(((uint64_t)2ULL), t6, &r7);
                if (o8) gm_trap("integer overflow in `*`", gm_srcc24pnihhgkug, 84, 18);
                uint64_t t9 = r7;
                uint64_t r10; bool o11 = __builtin_add_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 84, 18);
                uint64_t t12 = r10;
                gm_l5 = t12;
            }
            {
                uint64_t t14 = gm_l5;
                uint64_t t15 = gm_l2;
                uint8_t t16 = (t14 >= t15);
                if (!t16) goto ie13;
                {
                    goto lb3;
                }
                goto ix13;
ie13:;
ix13:;
            }
            {
                uint64_t t19 = gm_l5;
                uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
                if (o21) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 86, 12);
                uint64_t t22 = r20;
                uint64_t t23 = gm_l2;
                uint8_t t24 = (t22 < t23);
                if (!t24) goto ie18;
                gm_closure t25 = gm_l3;
                uint64_t t26 = gm_l5;
                uint64_t t27 = (uintptr_t)t26;
                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srcc24pnihhgkug, 86, 38);
                void * t29 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t27 * 24);
                uint64_t t30 = gm_l5;
                uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                if (o32) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 86, 51);
                uint64_t t33 = r31;
                uint64_t t34 = (uintptr_t)t33;
                uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_srcc24pnihhgkug, 86, 49);
                void * t36 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t34 * 24);
                gm_closure t37 = t25;
                gm_node *ccx38 = t37.cx;
                void * t39 = t37.code;
                uint8_t t40;
                if (GM_PTR_TAGGED(t39)) {
                    t40 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t39))(t29, t36);
                } else {
                    t40 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t39)(t37.env, ccx38, NULL, t29, t36);
                }
                if (gm_unw.kind) {
                    goto gm_ll4;
                }
                if (!t40) goto ie18;
                {
                    uint64_t t42 = gm_l5;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    if (o44) gm_trap("integer overflow in `+`", gm_srcc24pnihhgkug, 86, 65);
                    uint64_t t45 = r43;
                    gm_l5 = t45;
                }
                goto ix18;
ie18:;
ix18:;
            }
            {
                gm_closure t47 = gm_l3;
                uint64_t t48 = gm_l4;
                uint64_t t49 = (uintptr_t)t48;
                uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t49 >= (uintptr_t)(t50)) gm_trap_index(t49, t50, gm_srcc24pnihhgkug, 87, 22);
                void * t51 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t49 * 24);
                uint64_t t52 = gm_l5;
                uint64_t t53 = (uintptr_t)t52;
                uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t53 >= (uintptr_t)(t54)) gm_trap_index(t53, t54, gm_srcc24pnihhgkug, 87, 29);
                void * t55 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t53 * 24);
                gm_closure t56 = t47;
                gm_node *ccx57 = t56.cx;
                void * t58 = t56.code;
                uint8_t t59;
                if (GM_PTR_TAGGED(t58)) {
                    t59 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t58))(t51, t55);
                } else {
                    t59 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *, void *))t58)(t56.env, ccx57, NULL, t51, t55);
                }
                if (gm_unw.kind) {
                    goto gm_ll4;
                }
                uint8_t t60 = (uint8_t)!t59;
                if (!t60) goto ie46;
                {
                    goto lb3;
                }
                goto ix46;
ie46:;
ix46:;
            }
            {
                gm_b16_8 t62 = gm_l0;
                uint64_t t63 = gm_l4;
                uint64_t t64 = gm_l5;
                gm_f_swap_2ogc2g8(t62, t63, t64);
            }
            {
                uint64_t t65 = gm_l5;
                gm_l4 = t65;
            }
lc3:;
        }
gm_ll4:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb3; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc3; }
        }
        goto gm_lad;
lb3:;
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
extern __attribute__((aligned(4))) void gm_f_sort_by_m9qke1u(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_6vvt01e F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 158u);
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
        gm_b16_8 t15 = t13;
        gm_b16_8 t16 = t14;
        uint8_t t17 = (gm_eq0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16)));
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_swap_mehak34(gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_swap_mehak34 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* j */
    uint64_t gm_l3; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t3 >= (uintptr_t)(t4)) gm_trap_index(t3, t4, gm_srcc24pnihhgkug, 7, 10);
        uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t3 * 8));
        gm_l3 = t5;
    }
    {
        uint64_t t6 = gm_l1;
        uint64_t t7 = (uintptr_t)t6;
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srcc24pnihhgkug, 8, 5);
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcc24pnihhgkug, 8, 12);
        uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t10 * 8));
        GM_ST(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t7 * 8), t12);
    }
    {
        uint64_t t13 = gm_l2;
        uint64_t t14 = (uintptr_t)t13;
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcc24pnihhgkug, 9, 5);
        uint64_t t16 = gm_l3;
        GM_ST(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 8), t16);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_swap_mo06hda(gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_swap_mo06hda F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* j */
    gm_t3877pk7lv748 gm_l3; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t3 >= (uintptr_t)(t4)) gm_trap_index(t3, t4, gm_srcc24pnihhgkug, 7, 10);
        gm_t3877pk7lv748 t5 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t3 * 24));
        gm_l3 = t5;
    }
    {
        uint64_t t6 = gm_l1;
        uint64_t t7 = (uintptr_t)t6;
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srcc24pnihhgkug, 8, 5);
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcc24pnihhgkug, 8, 12);
        gm_t3877pk7lv748 t12 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t10 * 24));
        GM_ST(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t7 * 24), t12);
    }
    {
        uint64_t t13 = gm_l2;
        uint64_t t14 = (uintptr_t)t13;
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcc24pnihhgkug, 9, 5);
        gm_t3877pk7lv748 t16 = gm_l3;
        GM_ST(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 24), t16);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
