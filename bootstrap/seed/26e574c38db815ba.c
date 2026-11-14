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
struct gm_fr_push_rluch9r {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_regrow_bslikk9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bslikk9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
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
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l2;
                        uint8_t t19 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t18 + t17 * 1), t19);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t21 = gm_l4;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l4 = t24;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rluch9r F;
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
            gm_b16_8 t11 = gm_f_empty_slice_2p7d2kv();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_bslikk9(t10, ((uint64_t)1ULL), t12);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bslikk9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_bslikk9 F;
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
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
    GM_STACK_CHECK(&F, 144u);
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
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcpit8mdn0781q, 437, 107);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t7);
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
    GM_STACK_CHECK(&F, 6291u);
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
                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
                if (t35 != ((uint8_t)11ULL)) goto ie31;
                uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)&t34) + 8) + 0));
                gm_l8 = t36;
                gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t34) + 8) + 8));
                gm_l9 = t37;
                {
                    void * t39 = gm_l0;
                    gm_t88rcucg1b7uk t40 = gm_l9;
                    uint64_t t41 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t39, t40);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t42 = gm_f_us_cuv01fp(t41);
                    gm_l10 = t42;
                }
                {
                    {
                        gm_l12 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t44 = gm_l8;
                        uint64_t t45 = gm_f_us_cuv01fp(t44);
                        gm_l11 = t45;
                    }
                    {
                        for (;;) {
                            uint64_t t47 = gm_l12;
                            uint64_t t48 = gm_l11;
                            uint8_t t49 = (t47 < t48);
                            if (!t49) goto lb46;
                            {
                                uint64_t t52 = gm_l12;
                                uint64_t t55 = GM_LD(uint64_t, ((uint8_t *)&t52));
                                gm_l13 = t55;
                                {
                                    uint64_t t57 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    if (t57 >= (uintptr_t)(t58)) gm_trap_index(t57, t58, gm_srcns60k96jj1mt, 12, 22);
                                    uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t57 * 16) + 8);
                                    void * t60 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t57 * 16)));
                                    uint64_t t61 = gm_l13;
                                    uint64_t t62 = gm_l10;
                                    uint64_t r63; bool o64 = __builtin_mul_overflow(t61, t62, &r63);
                                    if (o64) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 12, 27);
                                    uint64_t t65 = r63;
                                    uint64_t t66 = (uintptr_t)t65;
                                    uint64_t t67 = gm_l13;
                                    uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                                    if (o69) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 12, 36);
                                    uint64_t t70 = r68;
                                    uint64_t t71 = gm_l10;
                                    uint64_t r72; bool o73 = __builtin_mul_overflow(t70, t71, &r72);
                                    if (o73) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 12, 35);
                                    uint64_t t74 = r72;
                                    uint64_t t75 = (uintptr_t)t74;
                                    if (t66 > t75 || t75 > (uintptr_t)t59) gm_trap_slice(t66, t75, t59, gm_srcns60k96jj1mt, 12, 22);
                                    gm_b16_8 t76;
                                    GM_ST(void *, (uint8_t *)&t76, (void *)(((uint8_t *)t60 + t66 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(t75 - t66));
                                    gm_l14 = t76;
                                }
                                {
                                    uint64_t t77 = (uintptr_t)((uint64_t)1ULL);
                                    uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    if (t77 >= (uintptr_t)(t78)) gm_trap_index(t77, t78, gm_srcns60k96jj1mt, 13, 22);
                                    uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t77 * 16) + 8);
                                    void * t80 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t77 * 16)));
                                    uint64_t t81 = gm_l13;
                                    uint64_t t82 = gm_l10;
                                    uint64_t r83; bool o84 = __builtin_mul_overflow(t81, t82, &r83);
                                    if (o84) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 13, 27);
                                    uint64_t t85 = r83;
                                    uint64_t t86 = (uintptr_t)t85;
                                    uint64_t t87 = gm_l13;
                                    uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                                    if (o89) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 13, 36);
                                    uint64_t t90 = r88;
                                    uint64_t t91 = gm_l10;
                                    uint64_t r92; bool o93 = __builtin_mul_overflow(t90, t91, &r92);
                                    if (o93) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 13, 35);
                                    uint64_t t94 = r92;
                                    uint64_t t95 = (uintptr_t)t94;
                                    if (t86 > t95 || t95 > (uintptr_t)t79) gm_trap_slice(t86, t95, t79, gm_srcns60k96jj1mt, 13, 22);
                                    gm_b16_8 t96;
                                    GM_ST(void *, (uint8_t *)&t96, (void *)(((uint8_t *)t80 + t86 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(t95 - t86));
                                    gm_l15 = t96;
                                }
                                {
                                    uint8_t t97;
                                    uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    switch (t106) { case ((uint8_t)0ULL): goto ma104; case ((uint8_t)1ULL): goto ma104; case ((uint8_t)2ULL): goto ma104; case ((uint8_t)3ULL): goto ma104; case ((uint8_t)4ULL): goto ma104; case ((uint8_t)5ULL): goto ma104; case ((uint8_t)6ULL): goto ma104; case ((uint8_t)7ULL): goto ma104; case ((uint8_t)8ULL): goto ma104; case ((uint8_t)9ULL): goto ma104; case ((uint8_t)10ULL): goto ma104; case ((uint8_t)11ULL): goto ma104; case ((uint8_t)12ULL): goto ma104; case ((uint8_t)13ULL): goto ma104; case ((uint8_t)14ULL): goto ma104; case ((uint8_t)15ULL): goto ma104; case ((uint8_t)16ULL): goto ma104; case ((uint8_t)17ULL): goto ma104; case ((uint8_t)18ULL): goto ma104; case ((uint8_t)19ULL): goto ma104; case ((uint8_t)20ULL): goto ma104; case ((uint8_t)21ULL): goto ma104; case ((uint8_t)22ULL): goto ma104; case ((uint8_t)23ULL): goto ma104; case ((uint8_t)24ULL): goto ma104; case ((uint8_t)25ULL): goto ma104; case ((uint8_t)26ULL): goto ma104; case ((uint8_t)27ULL): goto ma104; case ((uint8_t)28ULL): goto ma104; case ((uint8_t)29ULL): goto ma104; case ((uint8_t)30ULL): goto ma104; case ((uint8_t)31ULL): goto ma104; case ((uint8_t)32ULL): goto ma104; case ((uint8_t)33ULL): goto ma104; case ((uint8_t)34ULL): goto ma104; case ((uint8_t)35ULL): goto ma104; case ((uint8_t)36ULL): goto ma104; case ((uint8_t)37ULL): goto ma104; case ((uint8_t)38ULL): goto ma104; case ((uint8_t)39ULL): goto ma104; case ((uint8_t)40ULL): goto ma99; case ((uint8_t)41ULL): goto ma100; case ((uint8_t)42ULL): goto ma101; case ((uint8_t)43ULL): goto ma102; case ((uint8_t)44ULL): goto ma103; case ((uint8_t)45ULL): goto ma104; case ((uint8_t)46ULL): goto ma104; case ((uint8_t)47ULL): goto ma104; case ((uint8_t)48ULL): goto ma104; case ((uint8_t)49ULL): goto ma104; case ((uint8_t)50ULL): goto ma104; case ((uint8_t)51ULL): goto ma104; case ((uint8_t)52ULL): goto ma104; case ((uint8_t)53ULL): goto ma104; case ((uint8_t)54ULL): goto ma104; case ((uint8_t)55ULL): goto ma104; case ((uint8_t)56ULL): goto ma104; case ((uint8_t)57ULL): goto ma104; case ((uint8_t)58ULL): goto ma104; case ((uint8_t)59ULL): goto ma104; case ((uint8_t)60ULL): goto ma104; case ((uint8_t)61ULL): goto ma104; case ((uint8_t)62ULL): goto ma104; case ((uint8_t)63ULL): goto ma104; case ((uint8_t)64ULL): goto ma104; case ((uint8_t)65ULL): goto ma104; case ((uint8_t)66ULL): goto ma104; case ((uint8_t)67ULL): goto ma104; case ((uint8_t)68ULL): goto ma104; case ((uint8_t)69ULL): goto ma104; case ((uint8_t)70ULL): goto ma104; case ((uint8_t)71ULL): goto ma104; case ((uint8_t)72ULL): goto ma104; case ((uint8_t)73ULL): goto ma104; case ((uint8_t)74ULL): goto ma104; case ((uint8_t)75ULL): goto ma104; case ((uint8_t)76ULL): goto ma104; case ((uint8_t)77ULL): goto ma104; case ((uint8_t)78ULL): goto ma104; case ((uint8_t)79ULL): goto ma104; case ((uint8_t)80ULL): goto ma104; case ((uint8_t)81ULL): goto ma104; default: goto ma104; }
ma99:;
                                    uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    if (t108 != ((uint8_t)40ULL)) goto mn107;
                                    void * t109 = gm_l0;
                                    gm_tl874t2atnlh3 t110 = gm_l1;
                                    gm_t88rcucg1b7uk t111 = gm_l9;
                                    gm_b16_8 t112 = gm_l14;
                                    gm_b16_8 t113 = gm_l15;
                                    gm_t1ec8br9ddkav t114;
                                    gm_f_equal_e5vf82h((void *)&t114, (void *)NULL, cx, NULL, t109, t110, t111, t112, t113);
                                    if (gm_unw.kind) {
                                        goto gm_ll50;
                                    }
                                    if (((((uint8_t *)&t114))[0] != 0)) {
                                        gm_tcfa6l7reoosn t115;
                                        GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                                        ((uint8_t *)&t115)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t115 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t114) + 8)));
                                        F.ret = t115;
                                        goto gm_fret;
                                    }
                                    uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&t114) + 8));
                                    t97 = t116;
                                    goto me98;
mn107:;
ma100:;
                                    uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    if (t118 != ((uint8_t)41ULL)) goto mn117;
                                    void * t119 = gm_l0;
                                    gm_tl874t2atnlh3 t120 = gm_l1;
                                    gm_t88rcucg1b7uk t121 = gm_l9;
                                    gm_b16_8 t122 = gm_l14;
                                    gm_b16_8 t123 = gm_l15;
                                    gm_t1ec8br9ddkav t124;
                                    gm_f_equal_e5vf82h((void *)&t124, (void *)NULL, cx, NULL, t119, t120, t121, t122, t123);
                                    if (gm_unw.kind) {
                                        goto gm_ll50;
                                    }
                                    if (((((uint8_t *)&t124))[0] != 0)) {
                                        gm_tcfa6l7reoosn t125;
                                        GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                                        ((uint8_t *)&t125)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t125 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t124) + 8)));
                                        F.ret = t125;
                                        goto gm_fret;
                                    }
                                    uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&t124) + 8));
                                    uint8_t t127 = (uint8_t)!t126;
                                    t97 = t127;
                                    goto me98;
mn117:;
ma101:;
                                    uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    if (t129 != ((uint8_t)42ULL)) goto mn128;
                                    void * t130 = gm_l0;
                                    gm_tl874t2atnlh3 t131 = gm_l1;
                                    gm_ts6oqppjh0ba3 t132;
                                    GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)10ULL));
                                    gm_t88rcucg1b7uk t133 = gm_l9;
                                    gm_b16_8 t134 = gm_l14;
                                    gm_b16_8 t135 = gm_l15;
                                    gm_t1ec8br9ddkav t136;
                                    gm_f_cmp_op_mflafbf((void *)&t136, (void *)NULL, cx, NULL, t130, t131, t132, t133, t134, t135);
                                    if (gm_unw.kind) {
                                        goto gm_ll50;
                                    }
                                    if (((((uint8_t *)&t136))[0] != 0)) {
                                        gm_tcfa6l7reoosn t137;
                                        GM_ST(uint8_t, (uint8_t *)&t137 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t137 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t137 + 4, 0);
                                        ((uint8_t *)&t137)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t137 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t136) + 8)));
                                        F.ret = t137;
                                        goto gm_fret;
                                    }
                                    uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&t136) + 8));
                                    t97 = t138;
                                    goto me98;
mn128:;
ma102:;
                                    uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    if (t140 != ((uint8_t)43ULL)) goto mn139;
                                    void * t141 = gm_l0;
                                    gm_tl874t2atnlh3 t142 = gm_l1;
                                    gm_ts6oqppjh0ba3 t143;
                                    GM_ST(uint8_t, (uint8_t *)&t143 + 1, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t143 + 0), ((uint8_t)11ULL));
                                    gm_t88rcucg1b7uk t144 = gm_l9;
                                    gm_b16_8 t145 = gm_l14;
                                    gm_b16_8 t146 = gm_l15;
                                    gm_t1ec8br9ddkav t147;
                                    gm_f_cmp_op_mflafbf((void *)&t147, (void *)NULL, cx, NULL, t141, t142, t143, t144, t145, t146);
                                    if (gm_unw.kind) {
                                        goto gm_ll50;
                                    }
                                    if (((((uint8_t *)&t147))[0] != 0)) {
                                        gm_tcfa6l7reoosn t148;
                                        GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t148 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
                                        ((uint8_t *)&t148)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t148 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t147) + 8)));
                                        F.ret = t148;
                                        goto gm_fret;
                                    }
                                    uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&t147) + 8));
                                    t97 = t149;
                                    goto me98;
mn139:;
ma103:;
                                    uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    if (t151 != ((uint8_t)44ULL)) goto mn150;
                                    void * t152 = gm_l0;
                                    gm_tl874t2atnlh3 t153 = gm_l1;
                                    gm_ts6oqppjh0ba3 t154;
                                    GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)12ULL));
                                    gm_t88rcucg1b7uk t155 = gm_l9;
                                    gm_b16_8 t156 = gm_l14;
                                    gm_b16_8 t157 = gm_l15;
                                    gm_t1ec8br9ddkav t158;
                                    gm_f_cmp_op_mflafbf((void *)&t158, (void *)NULL, cx, NULL, t152, t153, t154, t155, t156, t157);
                                    if (gm_unw.kind) {
                                        goto gm_ll50;
                                    }
                                    if (((((uint8_t *)&t158))[0] != 0)) {
                                        gm_tcfa6l7reoosn t159;
                                        GM_ST(uint8_t, (uint8_t *)&t159 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t159 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t159 + 4, 0);
                                        ((uint8_t *)&t159)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t159 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t158) + 8)));
                                        F.ret = t159;
                                        goto gm_fret;
                                    }
                                    uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&t158) + 8));
                                    t97 = t160;
                                    goto me98;
mn150:;
ma104:;
                                    void * t162 = gm_l0;
                                    gm_tl874t2atnlh3 t163 = gm_l1;
                                    gm_ts6oqppjh0ba3 t164;
                                    GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t164 + 0), ((uint8_t)13ULL));
                                    gm_t88rcucg1b7uk t165 = gm_l9;
                                    gm_b16_8 t166 = gm_l14;
                                    gm_b16_8 t167 = gm_l15;
                                    gm_t1ec8br9ddkav t168;
                                    gm_f_cmp_op_mflafbf((void *)&t168, (void *)NULL, cx, NULL, t162, t163, t164, t165, t166, t167);
                                    if (gm_unw.kind) {
                                        goto gm_ll50;
                                    }
                                    if (((((uint8_t *)&t168))[0] != 0)) {
                                        gm_tcfa6l7reoosn t169;
                                        GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t169 + 4, 0);
                                        ((uint8_t *)&t169)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t169 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t168) + 8)));
                                        F.ret = t169;
                                        goto gm_fret;
                                    }
                                    uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&t168) + 8));
                                    t97 = t170;
                                    goto me98;
mn161:;
ma105:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 14, 22);
me98:;
                                    gm_l16 = t97;
                                }
                                {
                                    uint64_t t171 = gm_l13;
                                    uint64_t t172 = (uintptr_t)t171;
                                    uint64_t t173 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    if (t172 >= (uintptr_t)(t173)) gm_trap_index(t172, t173, gm_srcns60k96jj1mt, 22, 17);
                                    uint8_t *t174 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t172 * 1);
                                    uint8_t t175;
                                    uint8_t t177 = gm_l16;
                                    if (!t177) goto ie176;
                                    uint8_t t179 = ((uint8_t)1ULL);
                                    t175 = t179;
                                    goto ix176;
ie176:;
                                    uint8_t t181 = ((uint8_t)0ULL);
                                    t175 = t181;
ix176:;
                                    GM_ST(uint8_t, t174, t175);
                                }
                                goto me53;
mn54:;
                                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 11, 13);
me53:;
                            }
lc46:;
                            {
                                uint64_t t183 = gm_l12;
                                uint64_t r184; bool o185 = __builtin_add_overflow(t183, ((uint64_t)1ULL), &r184);
                                uint64_t t186 = r184;
                                gm_l12 = t186;
                            }
                        }
gm_ll50:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb46; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc46; }
                        }
                        goto gm_lad;
lb46:;
                    }
                }
                goto ix31;
ie31:;
                {
                    void * t188 = gm_l0;
                    gm_tl874t2atnlh3 t189 = gm_l1;
                    gm_b16_8 t190;
                    GM_ST(void *, (uint8_t *)&t190, (void *)(gm_s4lpfn3di4rp9));
                    GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(65));
                    gm_t76123gfet73g t191;
                    gm_f_trap_qh84l9d((void *)&t191, (void *)NULL, cx, NULL, t188, t189, t190);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t191);
                    goto gm_fdone;
                }
ix31:;
            }
            goto me3;
mn16:;
ma5:;
            uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t195 != ((uint8_t)46ULL)) goto pa194;
            goto po193;
pa194:;
            uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t197 != ((uint8_t)47ULL)) goto pa196;
            goto po193;
pa196:;
            uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t199 != ((uint8_t)48ULL)) goto pa198;
            goto po193;
pa198:;
            goto mn192;
po193:;
            {
                void * t202 = gm_l0;
                gm_t88rcucg1b7uk t203 = gm_l4;
                gm_t53at80pfdus3 t204;
                gm_f_kind_htv6093((void *)&t204, t202, GM_CONV(gm_csguankko4lmhd, t203));
                uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&t204) + 0));
                if (t205 != ((uint8_t)11ULL)) goto ie201;
                uint64_t t206 = GM_LD(uint64_t, ((((uint8_t *)&t204) + 8) + 0));
                gm_l17 = t206;
                gm_t88rcucg1b7uk t207 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t204) + 8) + 8));
                gm_l18 = t207;
                {
                    void * t209 = gm_l0;
                    gm_t88rcucg1b7uk t210 = gm_l18;
                    uint64_t t211 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t209, t210);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t212 = gm_f_us_cuv01fp(t211);
                    gm_l19 = t212;
                }
                {
                    void * t213 = gm_l0;
                    gm_t2lqu8jsj511d t214 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t213));
                    gm_l20 = t214;
                }
                {
                    void * t215 = gm_l0;
                    uint64_t t216 = gm_l19;
                    uint64_t t217 = (uint64_t)t216;
                    gm_b16_8 t218 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t215, t217);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l21 = t218;
                }
                {
                    gm_b16_8 t219 = gm_l21;
                    uint64_t t220 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t221 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t220 >= (uintptr_t)(t221)) gm_trap_index(t220, t221, gm_srcns60k96jj1mt, 30, 27);
                    uint64_t t222 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t220 * 16) + 8);
                    void * t223 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t220 * 16)));
                    uint64_t t224 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t225 = gm_l19;
                    uint64_t t226 = (uintptr_t)t225;
                    if (t224 > t226 || t226 > (uintptr_t)t222) gm_trap_slice(t224, t226, t222, gm_srcns60k96jj1mt, 30, 27);
                    gm_b16_8 t227;
                    GM_ST(void *, (uint8_t *)&t227, (void *)(((uint8_t *)t223 + t224 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t227 + 8, (uintptr_t)(t226 - t224));
                    gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&t227));
                    gm_f_copy_from_f7j6p05(t219, t228);
                }
                {
                    void * t229 = gm_l0;
                    uint64_t t230 = gm_l19;
                    uint64_t t231 = (uint64_t)t230;
                    gm_b16_8 t232 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t229, t231);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l22 = t232;
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
                        uint64_t t235 = gm_l17;
                        uint64_t t236 = gm_f_us_cuv01fp(t235);
                        gm_l24 = t236;
                    }
                    {
                        for (;;) {
                            uint64_t t238 = gm_l25;
                            uint64_t t239 = gm_l24;
                            uint8_t t240 = (t238 < t239);
                            if (!t240) goto lb237;
                            {
                                uint64_t t243 = gm_l25;
                                uint64_t t246 = GM_LD(uint64_t, ((uint8_t *)&t243));
                                gm_l26 = t246;
                                {
                                    uint64_t t248 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t249 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    if (t248 >= (uintptr_t)(t249)) gm_trap_index(t248, t249, gm_srcns60k96jj1mt, 35, 22);
                                    uint64_t t250 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t248 * 16) + 8);
                                    void * t251 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t248 * 16)));
                                    uint64_t t252 = gm_l26;
                                    uint64_t t253 = gm_l19;
                                    uint64_t r254; bool o255 = __builtin_mul_overflow(t252, t253, &r254);
                                    if (o255) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 35, 27);
                                    uint64_t t256 = r254;
                                    uint64_t t257 = (uintptr_t)t256;
                                    uint64_t t258 = gm_l26;
                                    uint64_t r259; bool o260 = __builtin_add_overflow(t258, ((uint64_t)1ULL), &r259);
                                    if (o260) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 35, 36);
                                    uint64_t t261 = r259;
                                    uint64_t t262 = gm_l19;
                                    uint64_t r263; bool o264 = __builtin_mul_overflow(t261, t262, &r263);
                                    if (o264) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 35, 35);
                                    uint64_t t265 = r263;
                                    uint64_t t266 = (uintptr_t)t265;
                                    if (t257 > t266 || t266 > (uintptr_t)t250) gm_trap_slice(t257, t266, t250, gm_srcns60k96jj1mt, 35, 22);
                                    gm_b16_8 t267;
                                    GM_ST(void *, (uint8_t *)&t267, (void *)(((uint8_t *)t251 + t257 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t267 + 8, (uintptr_t)(t266 - t257));
                                    gm_l27 = t267;
                                }
                                {
                                    uint8_t t269 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)46ULL)));
                                    if (!t269) goto ie268;
                                    {
                                        void * t271 = gm_l0;
                                        gm_tl874t2atnlh3 t272 = gm_l1;
                                        gm_t9qkh0rsooc8d t273;
                                        GM_ST(uint8_t, ((uint8_t *)&t273 + 0), ((uint8_t)0ULL));
                                        gm_ts6oqppjh0ba3 t274;
                                        GM_ST(uint8_t, ((uint8_t *)&t274 + 0), ((uint8_t)0ULL));
                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t274 + 1), t273);
                                        gm_t88rcucg1b7uk t275 = gm_l18;
                                        gm_t88rcucg1b7uk t276 = gm_l18;
                                        gm_t88rcucg1b7uk t277 = gm_l18;
                                        gm_b16_8 t278 = gm_l21;
                                        gm_b16_8 t279 = gm_l27;
                                        gm_b16_8 t280 = gm_l22;
                                        gm_tcfa6l7reoosn t281;
                                        gm_f_binop_nrsq3mp((void *)&t281, (void *)NULL, cx, NULL, t271, t272, t274, t275, t276, t277, t278, t279, t280);
                                        if (gm_unw.kind) {
                                            goto gm_ll241;
                                        }
                                        if (((((uint8_t *)&t281))[0] != 0)) {
                                            gm_tcfa6l7reoosn t282;
                                            GM_ST(uint8_t, (uint8_t *)&t282 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t282 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t282 + 4, 0);
                                            ((uint8_t *)&t282)[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t282 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t281) + 8)));
                                            F.ret = t282;
                                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                                            goto gm_ll241;
                                        }
                                        gm_b0_1 t283 = GM_LD(gm_b0_1, (((uint8_t *)&t281) + 8));
                                    }
                                    goto ix268;
ie268:;
                                    {
                                        void * t285 = (void *)((uint8_t *)&F.l23);
                                        gm_f_truncate_j7f0pmi(t285, ((uint64_t)0ULL));
                                    }
                                    {
                                        void * t286 = (void *)((uint8_t *)&F.l23);
                                        gm_b16_8 t287 = gm_l21;
                                        gm_tftb1odc739t5 t288 = gm_f_push_rluch9r(t286, t287);
                                        if (((((uint8_t *)&t288))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t288) + 1, gm_srcns60k96jj1mt, 40, 21);
                                        gm_b0_1 t289 = GM_LD(gm_b0_1, (((uint8_t *)&t288) + 1));
                                    }
                                    {
                                        void * t290 = (void *)((uint8_t *)&F.l23);
                                        gm_b16_8 t291 = gm_l27;
                                        gm_tftb1odc739t5 t292 = gm_f_push_rluch9r(t290, t291);
                                        if (((((uint8_t *)&t292))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t292) + 1, gm_srcns60k96jj1mt, 41, 21);
                                        gm_b0_1 t293 = GM_LD(gm_b0_1, (((uint8_t *)&t292) + 1));
                                    }
                                    {
                                        gm_tvdg94bk7aa22 t294;
                                        uint8_t t296 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)47ULL)));
                                        if (!t296) goto ie295;
                                        gm_tvdg94bk7aa22 t298;
                                        GM_ST(uint8_t, (uint8_t *)&t298 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t298 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t298 + 4, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t298 + 8, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t298 + 0), ((uint8_t)16ULL));
                                        gm_tvdg94bk7aa22 t299 = t298;
                                        t294 = t299;
                                        goto ix295;
ie295:;
                                        gm_tvdg94bk7aa22 t301;
                                        GM_ST(uint8_t, (uint8_t *)&t301 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t301 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t301 + 4, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t301 + 8, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t301 + 0), ((uint8_t)17ULL));
                                        gm_tvdg94bk7aa22 t302 = t301;
                                        t294 = t302;
ix295:;
                                        gm_l28 = t294;
                                    }
                                    {
                                        void * t303 = gm_l0;
                                        gm_tl874t2atnlh3 t304 = gm_l1;
                                        gm_tvdg94bk7aa22 t305 = gm_l28;
                                        gm_t88rcucg1b7uk t306 = gm_l18;
                                        gm_t88rcucg1b7uk t307 = gm_l18;
                                        gm_t88rcucg1b7uk t308 = gm_l18;
                                        gm_b16_8 t309 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
                                        gm_b16_8 t310 = gm_l22;
                                        gm_tcfa6l7reoosn t311;
                                        gm_f_apply_builtin_41dffti((void *)&t311, (void *)NULL, cx, NULL, t303, t304, t305, t306, t307, t308, t309, t310);
                                        if (gm_unw.kind) {
                                            goto gm_ll241;
                                        }
                                        if (((((uint8_t *)&t311))[0] != 0)) {
                                            gm_tcfa6l7reoosn t312;
                                            GM_ST(uint8_t, (uint8_t *)&t312 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t312 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t312 + 4, 0);
                                            ((uint8_t *)&t312)[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t312 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t311) + 8)));
                                            F.ret = t312;
                                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                                            goto gm_ll241;
                                        }
                                        gm_b0_1 t313 = GM_LD(gm_b0_1, (((uint8_t *)&t311) + 8));
                                    }
ix268:;
                                }
                                {
                                    gm_b16_8 t314 = gm_l21;
                                    gm_b16_8 t315 = gm_l22;
                                    gm_b16_8 t316 = GM_LD(gm_b16_8, ((uint8_t *)&t315));
                                    gm_f_copy_from_f7j6p05(t314, t316);
                                }
                                goto me244;
mn245:;
                                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 34, 13);
me244:;
                            }
lc237:;
                            {
                                uint64_t t318 = gm_l25;
                                uint64_t r319; bool o320 = __builtin_add_overflow(t318, ((uint64_t)1ULL), &r319);
                                uint64_t t321 = r319;
                                gm_l25 = t321;
                            }
                        }
gm_ll241:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb237; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc237; }
                        }
                        goto gm_ld208_1;
lb237:;
                    }
                }
                {
                    gm_b16_8 t322 = gm_l7;
                    gm_b16_8 t323 = gm_l21;
                    gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&t323));
                    gm_f_copy_from_f7j6p05(t322, t324);
                }
                {
                    void * t325 = gm_l0;
                    gm_t2lqu8jsj511d t326 = gm_l20;
                    gm_f_release_jn5kqdi(t325, GM_CONV(gm_csc7lc6jlkqkuq, t326));
                }
                {
                    void * t327 = (void *)((uint8_t *)&F.l23);
                    gm_f_deinit_tq06jtm(t327);
                }
                goto gm_ls328;
gm_ld208_1:;
                {
                    gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                    void * t329 = (void *)((uint8_t *)&F.l23);
                    gm_f_deinit_tq06jtm(t329);
                    gm_unw = gm_saved;
                }
                goto gm_lad;
gm_ls328:;
                goto ix201;
ie201:;
                {
                    void * t331 = gm_l0;
                    gm_tl874t2atnlh3 t332 = gm_l1;
                    gm_b16_8 t333;
                    GM_ST(void *, (uint8_t *)&t333, (void *)(gm_sm94b3smoinpd));
                    GM_ST(uintptr_t, (uint8_t *)&t333 + 8, (uintptr_t)(59));
                    gm_t76123gfet73g t334;
                    gm_f_trap_qh84l9d((void *)&t334, (void *)NULL, cx, NULL, t331, t332, t333);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t334);
                    goto gm_fdone;
                }
ix201:;
            }
            goto me3;
mn192:;
ma6:;
            uint8_t t336 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t336 != ((uint8_t)49ULL)) goto mn335;
            {
                gm_l29 = ((uint8_t)1);
            }
            {
                {
                    uint64_t t339 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t340 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t339 >= (uintptr_t)(t340)) gm_trap_index(t339, t340, gm_srcns60k96jj1mt, 52, 22);
                    gm_b16_8 t341 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t339 * 16));
                    gm_l30 = t341;
                }
                {
                    gm_l31 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t343 = gm_l31;
                        uint64_t t344 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                        uint8_t t345 = (t343 < t344);
                        if (!t345) goto lb342;
                        {
                            uint64_t t348 = gm_l31;
                            uint64_t t349 = (uintptr_t)t348;
                            uint64_t t350 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                            uint8_t t351 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t349 * 1));
                            uint8_t t354 = GM_LD(uint8_t, ((uint8_t *)&t351));
                            gm_l32 = t354;
                            {
                                uint8_t t357 = gm_l32;
                                uint8_t t358 = ((t357 == ((uint8_t)0ULL)));
                                if (!t358) goto ie356;
                                {
                                    gm_l29 = ((uint8_t)0);
                                }
                                goto ix356;
ie356:;
ix356:;
                            }
                            goto me352;
mn353:;
                            gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 52, 13);
me352:;
                        }
lc342:;
                        {
                            uint64_t t361 = gm_l31;
                            uint64_t r362; bool o363 = __builtin_add_overflow(t361, ((uint64_t)1ULL), &r362);
                            uint64_t t364 = r362;
                            gm_l31 = t364;
                        }
                    }
lb342:;
                }
            }
            {
                uint64_t t365 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t366 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                if (t365 >= (uintptr_t)(t366)) gm_trap_index(t365, t366, gm_srcns60k96jj1mt, 55, 13);
                uint8_t *t367 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t365 * 1);
                uint8_t t368;
                uint8_t t370 = gm_l29;
                if (!t370) goto ie369;
                uint8_t t372 = ((uint8_t)1ULL);
                t368 = t372;
                goto ix369;
ie369:;
                uint8_t t374 = ((uint8_t)0ULL);
                t368 = t374;
ix369:;
                GM_ST(uint8_t, t367, t368);
            }
            goto me3;
mn335:;
ma7:;
            uint8_t t376 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t376 != ((uint8_t)50ULL)) goto mn375;
            {
                gm_l33 = ((uint8_t)0);
            }
            {
                {
                    uint64_t t379 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t380 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t379 >= (uintptr_t)(t380)) gm_trap_index(t379, t380, gm_srcns60k96jj1mt, 59, 22);
                    gm_b16_8 t381 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t379 * 16));
                    gm_l34 = t381;
                }
                {
                    gm_l35 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t383 = gm_l35;
                        uint64_t t384 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                        uint8_t t385 = (t383 < t384);
                        if (!t385) goto lb382;
                        {
                            uint64_t t388 = gm_l35;
                            uint64_t t389 = (uintptr_t)t388;
                            uint64_t t390 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                            uint8_t t391 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t389 * 1));
                            uint8_t t394 = GM_LD(uint8_t, ((uint8_t *)&t391));
                            gm_l36 = t394;
                            {
                                uint8_t t397 = gm_l36;
                                uint8_t t398 = (!(t397 == ((uint8_t)0ULL)));
                                if (!t398) goto ie396;
                                {
                                    gm_l33 = ((uint8_t)1);
                                }
                                goto ix396;
ie396:;
ix396:;
                            }
                            goto me392;
mn393:;
                            gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 59, 13);
me392:;
                        }
lc382:;
                        {
                            uint64_t t401 = gm_l35;
                            uint64_t r402; bool o403 = __builtin_add_overflow(t401, ((uint64_t)1ULL), &r402);
                            uint64_t t404 = r402;
                            gm_l35 = t404;
                        }
                    }
lb382:;
                }
            }
            {
                uint64_t t405 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t406 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                if (t405 >= (uintptr_t)(t406)) gm_trap_index(t405, t406, gm_srcns60k96jj1mt, 62, 13);
                uint8_t *t407 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t405 * 1);
                uint8_t t408;
                uint8_t t410 = gm_l33;
                if (!t410) goto ie409;
                uint8_t t412 = ((uint8_t)1ULL);
                t408 = t412;
                goto ix409;
ie409:;
                uint8_t t414 = ((uint8_t)0ULL);
                t408 = t414;
ix409:;
                GM_ST(uint8_t, t407, t408);
            }
            goto me3;
mn375:;
ma8:;
            uint8_t t416 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t416 != ((uint8_t)51ULL)) goto mn415;
            {
                gm_l37 = ((uint64_t)0ULL);
            }
            {
                {
                    uint64_t t419 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t420 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t419 >= (uintptr_t)(t420)) gm_trap_index(t419, t420, gm_srcns60k96jj1mt, 66, 22);
                    gm_b16_8 t421 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t419 * 16));
                    gm_l38 = t421;
                }
                {
                    gm_l39 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t423 = gm_l39;
                        uint64_t t424 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                        uint8_t t425 = (t423 < t424);
                        if (!t425) goto lb422;
                        {
                            uint64_t t428 = gm_l39;
                            uint64_t t429 = (uintptr_t)t428;
                            uint64_t t430 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                            uint8_t t431 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t429 * 1));
                            uint8_t t434 = GM_LD(uint8_t, ((uint8_t *)&t431));
                            gm_l40 = t434;
                            {
                                uint8_t t437 = gm_l40;
                                uint8_t t438 = (!(t437 == ((uint8_t)0ULL)));
                                if (!t438) goto ie436;
                                {
                                    uint64_t t440 = gm_l37;
                                    uint64_t r441; bool o442 = __builtin_add_overflow(t440, ((uint64_t)1ULL), &r441);
                                    if (o442) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 67, 33);
                                    uint64_t t443 = r441;
                                    gm_l37 = t443;
                                }
                                goto ix436;
ie436:;
ix436:;
                            }
                            goto me432;
mn433:;
                            gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 66, 13);
me432:;
                        }
lc422:;
                        {
                            uint64_t t445 = gm_l39;
                            uint64_t r446; bool o447 = __builtin_add_overflow(t445, ((uint64_t)1ULL), &r446);
                            uint64_t t448 = r446;
                            gm_l39 = t448;
                        }
                    }
lb422:;
                }
            }
            {
                void * t449 = gm_l0;
                gm_b16_8 t450 = gm_l7;
                uint64_t t451 = gm_l37;
                unsigned __int128 t452 = (unsigned __int128)t451;
                gm_f_put_uint_cddin99(t449, t450, t452);
            }
            goto me3;
mn415:;
ma9:;
            uint8_t t454 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t454 != ((uint8_t)52ULL)) goto mn453;
            {
                void * t457 = gm_l0;
                gm_t88rcucg1b7uk t458 = gm_l5;
                gm_t53at80pfdus3 t459;
                gm_f_kind_htv6093((void *)&t459, t457, GM_CONV(gm_csguankko4lmhd, t458));
                uint8_t t460 = GM_LD(uint8_t, (((uint8_t *)&t459) + 0));
                if (t460 != ((uint8_t)11ULL)) goto ie456;
                uint64_t t461 = GM_LD(uint64_t, ((((uint8_t *)&t459) + 8) + 0));
                gm_l41 = t461;
                gm_t88rcucg1b7uk t462 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t459) + 8) + 8));
                gm_l42 = t462;
                {
                    void * t464 = gm_l0;
                    gm_t88rcucg1b7uk t465 = gm_l42;
                    uint64_t t466 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t464, t465);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t467 = gm_f_us_cuv01fp(t466);
                    gm_l43 = t467;
                }
                {
                    gm_b16_8 t468 = gm_l7;
                    uint64_t t469 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t470 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t469 >= (uintptr_t)(t470)) gm_trap_index(t469, t470, gm_srcns60k96jj1mt, 74, 27);
                    gm_b16_8 t471 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t469 * 16));
                    gm_b16_8 t472 = GM_LD(gm_b16_8, ((uint8_t *)&t471));
                    gm_f_copy_from_f7j6p05(t468, t472);
                }
                {
                    {
                        gm_l45 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t474 = gm_l41;
                        uint64_t t475 = gm_f_us_cuv01fp(t474);
                        gm_l44 = t475;
                    }
                    {
                        for (;;) {
                            uint64_t t477 = gm_l45;
                            uint64_t t478 = gm_l44;
                            uint8_t t479 = (t477 < t478);
                            if (!t479) goto lb476;
                            {
                                uint64_t t482 = gm_l45;
                                uint64_t t485 = GM_LD(uint64_t, ((uint8_t *)&t482));
                                gm_l46 = t485;
                                {
                                    uint64_t t488 = gm_l46;
                                    uint64_t t489 = (uintptr_t)t488;
                                    uint64_t t490 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t491 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    if (t490 >= (uintptr_t)(t491)) gm_trap_index(t490, t491, gm_srcns60k96jj1mt, 76, 20);
                                    uint64_t t492 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t490 * 16) + 8);
                                    if (t489 >= (uintptr_t)(t492)) gm_trap_index(t489, t492, gm_srcns60k96jj1mt, 76, 20);
                                    uint8_t t493 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t490 * 16)) + t489 * 1));
                                    uint8_t t494 = (!(t493 == ((uint8_t)0ULL)));
                                    if (!t494) goto ie487;
                                    {
                                        uint64_t t496 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                        void * t497 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                                        uint64_t t498 = gm_l46;
                                        uint64_t t499 = gm_l43;
                                        uint64_t r500; bool o501 = __builtin_mul_overflow(t498, t499, &r500);
                                        if (o501) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 76, 43);
                                        uint64_t t502 = r500;
                                        uint64_t t503 = (uintptr_t)t502;
                                        uint64_t t504 = gm_l46;
                                        uint64_t r505; bool o506 = __builtin_add_overflow(t504, ((uint64_t)1ULL), &r505);
                                        if (o506) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 76, 52);
                                        uint64_t t507 = r505;
                                        uint64_t t508 = gm_l43;
                                        uint64_t r509; bool o510 = __builtin_mul_overflow(t507, t508, &r509);
                                        if (o510) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 76, 51);
                                        uint64_t t511 = r509;
                                        uint64_t t512 = (uintptr_t)t511;
                                        if (t503 > t512 || t512 > (uintptr_t)t496) gm_trap_slice(t503, t512, t496, gm_srcns60k96jj1mt, 76, 39);
                                        gm_b16_8 t513;
                                        GM_ST(void *, (uint8_t *)&t513, (void *)(((uint8_t *)t497 + t503 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t513 + 8, (uintptr_t)(t512 - t503));
                                        uint64_t t514 = (uintptr_t)((uint64_t)1ULL);
                                        uint64_t t515 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                        if (t514 >= (uintptr_t)(t515)) gm_trap_index(t514, t515, gm_srcns60k96jj1mt, 76, 75);
                                        uint64_t t516 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t514 * 16) + 8);
                                        void * t517 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t514 * 16)));
                                        uint64_t t518 = gm_l46;
                                        uint64_t t519 = gm_l43;
                                        uint64_t r520; bool o521 = __builtin_mul_overflow(t518, t519, &r520);
                                        if (o521) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 76, 80);
                                        uint64_t t522 = r520;
                                        uint64_t t523 = (uintptr_t)t522;
                                        uint64_t t524 = gm_l46;
                                        uint64_t r525; bool o526 = __builtin_add_overflow(t524, ((uint64_t)1ULL), &r525);
                                        if (o526) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 76, 89);
                                        uint64_t t527 = r525;
                                        uint64_t t528 = gm_l43;
                                        uint64_t r529; bool o530 = __builtin_mul_overflow(t527, t528, &r529);
                                        if (o530) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 76, 88);
                                        uint64_t t531 = r529;
                                        uint64_t t532 = (uintptr_t)t531;
                                        if (t523 > t532 || t532 > (uintptr_t)t516) gm_trap_slice(t523, t532, t516, gm_srcns60k96jj1mt, 76, 75);
                                        gm_b16_8 t533;
                                        GM_ST(void *, (uint8_t *)&t533, (void *)(((uint8_t *)t517 + t523 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t533 + 8, (uintptr_t)(t532 - t523));
                                        gm_b16_8 t534 = GM_LD(gm_b16_8, ((uint8_t *)&t533));
                                        gm_f_copy_from_f7j6p05(t513, t534);
                                    }
                                    goto ix487;
ie487:;
ix487:;
                                }
                                goto me483;
mn484:;
                                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 75, 13);
me483:;
                            }
lc476:;
                            {
                                uint64_t t536 = gm_l45;
                                uint64_t r537; bool o538 = __builtin_add_overflow(t536, ((uint64_t)1ULL), &r537);
                                uint64_t t539 = r537;
                                gm_l45 = t539;
                            }
                        }
lb476:;
                    }
                }
                goto ix456;
ie456:;
                {
                    void * t541 = gm_l0;
                    gm_tl874t2atnlh3 t542 = gm_l1;
                    gm_b16_8 t543;
                    GM_ST(void *, (uint8_t *)&t543, (void *)(gm_spqlqjii789ln));
                    GM_ST(uintptr_t, (uint8_t *)&t543 + 8, (uintptr_t)(56));
                    gm_t76123gfet73g t544;
                    gm_f_trap_qh84l9d((void *)&t544, (void *)NULL, cx, NULL, t541, t542, t543);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t544);
                    goto gm_fdone;
                }
ix456:;
            }
            goto me3;
mn453:;
ma10:;
            uint8_t t548 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t548 != ((uint8_t)53ULL)) goto pa547;
            goto po546;
pa547:;
            uint8_t t550 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t550 != ((uint8_t)54ULL)) goto pa549;
            goto po546;
pa549:;
            uint8_t t552 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t552 != ((uint8_t)55ULL)) goto pa551;
            goto po546;
pa551:;
            goto mn545;
po546:;
            {
                void * t555 = gm_l0;
                gm_t88rcucg1b7uk t556 = gm_l4;
                gm_t53at80pfdus3 t557;
                gm_f_kind_htv6093((void *)&t557, t555, GM_CONV(gm_csguankko4lmhd, t556));
                uint8_t t558 = GM_LD(uint8_t, (((uint8_t *)&t557) + 0));
                if (t558 != ((uint8_t)11ULL)) goto ie554;
                uint64_t t559 = GM_LD(uint64_t, ((((uint8_t *)&t557) + 8) + 0));
                gm_l47 = t559;
                gm_t88rcucg1b7uk t560 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t557) + 8) + 8));
                gm_l48 = t560;
                {
                    void * t562 = gm_l0;
                    gm_t88rcucg1b7uk t563 = gm_l48;
                    uint64_t t564 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t562, t563);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t565 = gm_f_us_cuv01fp(t564);
                    gm_l49 = t565;
                }
                {
                    gm_tvdg94bk7aa22 t566;
                    uint8_t t572 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    switch (t572) { case ((uint8_t)0ULL): goto ma570; case ((uint8_t)1ULL): goto ma570; case ((uint8_t)2ULL): goto ma570; case ((uint8_t)3ULL): goto ma570; case ((uint8_t)4ULL): goto ma570; case ((uint8_t)5ULL): goto ma570; case ((uint8_t)6ULL): goto ma570; case ((uint8_t)7ULL): goto ma570; case ((uint8_t)8ULL): goto ma570; case ((uint8_t)9ULL): goto ma570; case ((uint8_t)10ULL): goto ma570; case ((uint8_t)11ULL): goto ma570; case ((uint8_t)12ULL): goto ma570; case ((uint8_t)13ULL): goto ma570; case ((uint8_t)14ULL): goto ma570; case ((uint8_t)15ULL): goto ma570; case ((uint8_t)16ULL): goto ma570; case ((uint8_t)17ULL): goto ma570; case ((uint8_t)18ULL): goto ma570; case ((uint8_t)19ULL): goto ma570; case ((uint8_t)20ULL): goto ma570; case ((uint8_t)21ULL): goto ma570; case ((uint8_t)22ULL): goto ma570; case ((uint8_t)23ULL): goto ma570; case ((uint8_t)24ULL): goto ma570; case ((uint8_t)25ULL): goto ma570; case ((uint8_t)26ULL): goto ma570; case ((uint8_t)27ULL): goto ma570; case ((uint8_t)28ULL): goto ma570; case ((uint8_t)29ULL): goto ma570; case ((uint8_t)30ULL): goto ma570; case ((uint8_t)31ULL): goto ma570; case ((uint8_t)32ULL): goto ma570; case ((uint8_t)33ULL): goto ma570; case ((uint8_t)34ULL): goto ma570; case ((uint8_t)35ULL): goto ma570; case ((uint8_t)36ULL): goto ma570; case ((uint8_t)37ULL): goto ma570; case ((uint8_t)38ULL): goto ma570; case ((uint8_t)39ULL): goto ma570; case ((uint8_t)40ULL): goto ma570; case ((uint8_t)41ULL): goto ma570; case ((uint8_t)42ULL): goto ma570; case ((uint8_t)43ULL): goto ma570; case ((uint8_t)44ULL): goto ma570; case ((uint8_t)45ULL): goto ma570; case ((uint8_t)46ULL): goto ma570; case ((uint8_t)47ULL): goto ma570; case ((uint8_t)48ULL): goto ma570; case ((uint8_t)49ULL): goto ma570; case ((uint8_t)50ULL): goto ma570; case ((uint8_t)51ULL): goto ma570; case ((uint8_t)52ULL): goto ma570; case ((uint8_t)53ULL): goto ma568; case ((uint8_t)54ULL): goto ma569; case ((uint8_t)55ULL): goto ma570; case ((uint8_t)56ULL): goto ma570; case ((uint8_t)57ULL): goto ma570; case ((uint8_t)58ULL): goto ma570; case ((uint8_t)59ULL): goto ma570; case ((uint8_t)60ULL): goto ma570; case ((uint8_t)61ULL): goto ma570; case ((uint8_t)62ULL): goto ma570; case ((uint8_t)63ULL): goto ma570; case ((uint8_t)64ULL): goto ma570; case ((uint8_t)65ULL): goto ma570; case ((uint8_t)66ULL): goto ma570; case ((uint8_t)67ULL): goto ma570; case ((uint8_t)68ULL): goto ma570; case ((uint8_t)69ULL): goto ma570; case ((uint8_t)70ULL): goto ma570; case ((uint8_t)71ULL): goto ma570; case ((uint8_t)72ULL): goto ma570; case ((uint8_t)73ULL): goto ma570; case ((uint8_t)74ULL): goto ma570; case ((uint8_t)75ULL): goto ma570; case ((uint8_t)76ULL): goto ma570; case ((uint8_t)77ULL): goto ma570; case ((uint8_t)78ULL): goto ma570; case ((uint8_t)79ULL): goto ma570; case ((uint8_t)80ULL): goto ma570; case ((uint8_t)81ULL): goto ma570; default: goto ma570; }
ma568:;
                    uint8_t t574 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t574 != ((uint8_t)53ULL)) goto mn573;
                    gm_tvdg94bk7aa22 t575;
                    GM_ST(uint8_t, (uint8_t *)&t575 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t575 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t575 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t575 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t575 + 0), ((uint8_t)16ULL));
                    t566 = t575;
                    goto me567;
mn573:;
ma569:;
                    uint8_t t577 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t577 != ((uint8_t)54ULL)) goto mn576;
                    gm_tvdg94bk7aa22 t578;
                    GM_ST(uint8_t, (uint8_t *)&t578 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t578 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t578 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t578 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t578 + 0), ((uint8_t)17ULL));
                    t566 = t578;
                    goto me567;
mn576:;
ma570:;
                    gm_tvdg94bk7aa22 t580;
                    GM_ST(uint8_t, (uint8_t *)&t580 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t580 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t580 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t580 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t580 + 0), ((uint8_t)19ULL));
                    t566 = t580;
                    goto me567;
mn579:;
ma571:;
                    gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 82, 35);
me567:;
                    gm_l50 = t566;
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
                        uint64_t t583 = gm_l47;
                        uint64_t t584 = gm_f_us_cuv01fp(t583);
                        gm_l52 = t584;
                    }
                    {
                        for (;;) {
                            uint64_t t586 = gm_l53;
                            uint64_t t587 = gm_l52;
                            uint8_t t588 = (t586 < t587);
                            if (!t588) goto lb585;
                            {
                                uint64_t t591 = gm_l53;
                                uint64_t t594 = GM_LD(uint64_t, ((uint8_t *)&t591));
                                gm_l54 = t594;
                                {
                                    void * t596 = (void *)((uint8_t *)&F.l51);
                                    gm_f_truncate_j7f0pmi(t596, ((uint64_t)0ULL));
                                }
                                {
                                    {
                                        gm_b16_8 t598 = gm_l6;
                                        gm_l55 = t598;
                                    }
                                    {
                                        gm_l56 = ((uint64_t)0ULL);
                                    }
                                    {
                                        for (;;) {
                                            uint64_t t600 = gm_l56;
                                            uint64_t t601 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                                            uint8_t t602 = (t600 < t601);
                                            if (!t602) goto lb599;
                                            {
                                                uint64_t t605 = gm_l56;
                                                uint64_t t606 = (uintptr_t)t605;
                                                uint64_t t607 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                                                gm_b16_8 t610 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l55)) + t606 * 16));
                                                gm_l57 = t610;
                                                {
                                                    void * t612 = (void *)((uint8_t *)&F.l51);
                                                    uint64_t t613 = GM_LD(uintptr_t, ((uint8_t *)&gm_l57) + 8);
                                                    void * t614 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l57)));
                                                    uint64_t t615 = gm_l54;
                                                    uint64_t t616 = gm_l49;
                                                    uint64_t r617; bool o618 = __builtin_mul_overflow(t615, t616, &r617);
                                                    if (o618) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 91, 42);
                                                    uint64_t t619 = r617;
                                                    uint64_t t620 = (uintptr_t)t619;
                                                    uint64_t t621 = gm_l54;
                                                    uint64_t r622; bool o623 = __builtin_add_overflow(t621, ((uint64_t)1ULL), &r622);
                                                    if (o623) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 91, 51);
                                                    uint64_t t624 = r622;
                                                    uint64_t t625 = gm_l49;
                                                    uint64_t r626; bool o627 = __builtin_mul_overflow(t624, t625, &r626);
                                                    if (o627) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 91, 50);
                                                    uint64_t t628 = r626;
                                                    uint64_t t629 = (uintptr_t)t628;
                                                    if (t620 > t629 || t629 > (uintptr_t)t613) gm_trap_slice(t620, t629, t613, gm_srcns60k96jj1mt, 91, 40);
                                                    gm_b16_8 t630;
                                                    GM_ST(void *, (uint8_t *)&t630, (void *)(((uint8_t *)t614 + t620 * 1)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t630 + 8, (uintptr_t)(t629 - t620));
                                                    gm_tftb1odc739t5 t631 = gm_f_push_rluch9r(t612, t630);
                                                    if (((((uint8_t *)&t631))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t631) + 1, gm_srcns60k96jj1mt, 91, 30);
                                                    gm_b0_1 t632 = GM_LD(gm_b0_1, (((uint8_t *)&t631) + 1));
                                                }
                                                goto me608;
mn609:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 91, 17);
me608:;
                                            }
lc599:;
                                            {
                                                uint64_t t634 = gm_l56;
                                                uint64_t r635; bool o636 = __builtin_add_overflow(t634, ((uint64_t)1ULL), &r635);
                                                uint64_t t637 = r635;
                                                gm_l56 = t637;
                                            }
                                        }
lb599:;
                                    }
                                }
                                {
                                    void * t638 = gm_l0;
                                    gm_tl874t2atnlh3 t639 = gm_l1;
                                    gm_tvdg94bk7aa22 t640 = gm_l50;
                                    gm_t88rcucg1b7uk t641 = gm_l48;
                                    gm_t88rcucg1b7uk t642 = gm_l48;
                                    gm_t88rcucg1b7uk t643 = gm_l48;
                                    gm_b16_8 t644 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
                                    uint64_t t645 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    void * t646 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                                    uint64_t t647 = gm_l54;
                                    uint64_t t648 = gm_l49;
                                    uint64_t r649; bool o650 = __builtin_mul_overflow(t647, t648, &r649);
                                    if (o650) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 92, 85);
                                    uint64_t t651 = r649;
                                    uint64_t t652 = (uintptr_t)t651;
                                    uint64_t t653 = gm_l54;
                                    uint64_t r654; bool o655 = __builtin_add_overflow(t653, ((uint64_t)1ULL), &r654);
                                    if (o655) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 92, 94);
                                    uint64_t t656 = r654;
                                    uint64_t t657 = gm_l49;
                                    uint64_t r658; bool o659 = __builtin_mul_overflow(t656, t657, &r658);
                                    if (o659) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 92, 93);
                                    uint64_t t660 = r658;
                                    uint64_t t661 = (uintptr_t)t660;
                                    if (t652 > t661 || t661 > (uintptr_t)t645) gm_trap_slice(t652, t661, t645, gm_srcns60k96jj1mt, 92, 81);
                                    gm_b16_8 t662;
                                    GM_ST(void *, (uint8_t *)&t662, (void *)(((uint8_t *)t646 + t652 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t662 + 8, (uintptr_t)(t661 - t652));
                                    gm_tcfa6l7reoosn t663;
                                    gm_f_apply_builtin_41dffti((void *)&t663, (void *)NULL, cx, NULL, t638, t639, t640, t641, t642, t643, t644, t662);
                                    if (gm_unw.kind) {
                                        goto gm_ll589;
                                    }
                                    if (((((uint8_t *)&t663))[0] != 0)) {
                                        gm_tcfa6l7reoosn t664;
                                        GM_ST(uint8_t, (uint8_t *)&t664 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t664 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t664 + 4, 0);
                                        ((uint8_t *)&t664)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t664 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t663) + 8)));
                                        F.ret = t664;
                                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                                        goto gm_ll589;
                                    }
                                    gm_b0_1 t665 = GM_LD(gm_b0_1, (((uint8_t *)&t663) + 8));
                                }
                                goto me592;
mn593:;
                                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 89, 13);
me592:;
                            }
lc585:;
                            {
                                uint64_t t667 = gm_l53;
                                uint64_t r668; bool o669 = __builtin_add_overflow(t667, ((uint64_t)1ULL), &r668);
                                uint64_t t670 = r668;
                                gm_l53 = t670;
                            }
                        }
gm_ll589:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb585; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc585; }
                        }
                        goto gm_ld561_1;
lb585:;
                    }
                }
                {
                    void * t671 = (void *)((uint8_t *)&F.l51);
                    gm_f_deinit_tq06jtm(t671);
                }
                goto gm_ls672;
gm_ld561_1:;
                {
                    gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                    void * t673 = (void *)((uint8_t *)&F.l51);
                    gm_f_deinit_tq06jtm(t673);
                    gm_unw = gm_saved;
                }
                goto gm_lad;
gm_ls672:;
                goto ix554;
ie554:;
                {
                    void * t675 = gm_l0;
                    gm_tl874t2atnlh3 t676 = gm_l1;
                    gm_b16_8 t677;
                    GM_ST(void *, (uint8_t *)&t677, (void *)(gm_scqqkvgm1j3cs));
                    GM_ST(uintptr_t, (uint8_t *)&t677 + 8, (uintptr_t)(64));
                    gm_t76123gfet73g t678;
                    gm_f_trap_qh84l9d((void *)&t678, (void *)NULL, cx, NULL, t675, t676, t677);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t678);
                    goto gm_fdone;
                }
ix554:;
            }
            goto me3;
mn545:;
ma11:;
            uint8_t t680 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t680 != ((uint8_t)56ULL)) goto mn679;
            {
                void * t683 = gm_l0;
                gm_t88rcucg1b7uk t684 = gm_l4;
                gm_t53at80pfdus3 t685;
                gm_f_kind_htv6093((void *)&t685, t683, GM_CONV(gm_csguankko4lmhd, t684));
                uint8_t t686 = GM_LD(uint8_t, (((uint8_t *)&t685) + 0));
                if (t686 != ((uint8_t)11ULL)) goto ie682;
                uint64_t t687 = GM_LD(uint64_t, ((((uint8_t *)&t685) + 8) + 0));
                gm_l58 = t687;
                gm_t88rcucg1b7uk t688 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t685) + 8) + 8));
                gm_l59 = t688;
                {
                    void * t691 = gm_l0;
                    gm_t88rcucg1b7uk t692 = gm_l3;
                    gm_t53at80pfdus3 t693;
                    gm_f_kind_htv6093((void *)&t693, t691, GM_CONV(gm_csguankko4lmhd, t692));
                    uint8_t t694 = GM_LD(uint8_t, (((uint8_t *)&t693) + 0));
                    if (t694 != ((uint8_t)11ULL)) goto ie690;
                    gm_t88rcucg1b7uk t695 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t693) + 8) + 8));
                    gm_l60 = t695;
                    {
                        void * t697 = gm_l0;
                        gm_t88rcucg1b7uk t698 = gm_l59;
                        uint64_t t699 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t697, t698);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t700 = gm_f_us_cuv01fp(t699);
                        gm_l61 = t700;
                    }
                    {
                        void * t701 = gm_l0;
                        gm_t88rcucg1b7uk t702 = gm_l60;
                        uint64_t t703 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t701, t702);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t704 = gm_f_us_cuv01fp(t703);
                        gm_l62 = t704;
                    }
                    {
                        gm_tvdg94bk7aa22 t705;
                        void * t707 = gm_l0;
                        gm_t88rcucg1b7uk t708 = gm_l60;
                        gm_t53at80pfdus3 t709;
                        gm_f_kind_htv6093((void *)&t709, t707, GM_CONV(gm_csguankko4lmhd, t708));
                        uint8_t t710;
                        uint8_t t713 = GM_LD(uint8_t, (((uint8_t *)&t709) + 0));
                        if (t713 != ((uint8_t)3ULL)) goto mn712;
                        t710 = ((uint8_t)1);
                        goto me711;
mn712:;
                        t710 = ((uint8_t)0);
                        goto me711;
mn714:;
                        gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 100, 35);
me711:;
                        if (!t710) goto ie706;
                        gm_tvdg94bk7aa22 t716;
                        GM_ST(uint8_t, (uint8_t *)&t716 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t716 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t716 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t716 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t716 + 0), ((uint8_t)2ULL));
                        gm_tvdg94bk7aa22 t717 = t716;
                        t705 = t717;
                        goto ix706;
ie706:;
                        gm_tvdg94bk7aa22 t719;
                        GM_ST(uint8_t, (uint8_t *)&t719 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t719 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t719 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t719 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t719 + 0), ((uint8_t)0ULL));
                        gm_tvdg94bk7aa22 t720 = t719;
                        t705 = t720;
ix706:;
                        gm_l63 = t705;
                    }
                    {
                        {
                            gm_l65 = ((uint64_t)0ULL);
                        }
                        {
                            uint64_t t722 = gm_l58;
                            uint64_t t723 = gm_f_us_cuv01fp(t722);
                            gm_l64 = t723;
                        }
                        {
                            for (;;) {
                                uint64_t t725 = gm_l65;
                                uint64_t t726 = gm_l64;
                                uint8_t t727 = (t725 < t726);
                                if (!t727) goto lb724;
                                {
                                    uint64_t t730 = gm_l65;
                                    uint64_t t733 = GM_LD(uint64_t, ((uint8_t *)&t730));
                                    gm_l66 = t733;
                                    {
                                        void * t735 = gm_l0;
                                        gm_tl874t2atnlh3 t736 = gm_l1;
                                        gm_tvdg94bk7aa22 t737 = gm_l63;
                                        gm_t88rcucg1b7uk t738 = gm_l59;
                                        gm_t88rcucg1b7uk t739 = gm_l60;
                                        uint64_t t740 = (uintptr_t)((uint64_t)0ULL);
                                        uint64_t t741 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                        if (t740 >= (uintptr_t)(t741)) gm_trap_index(t740, t741, gm_srcns60k96jj1mt, 101, 68);
                                        uint64_t t742 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t740 * 16) + 8);
                                        void * t743 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t740 * 16)));
                                        uint64_t t744 = gm_l66;
                                        uint64_t t745 = gm_l61;
                                        uint64_t r746; bool o747 = __builtin_mul_overflow(t744, t745, &r746);
                                        if (o747) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 101, 73);
                                        uint64_t t748 = r746;
                                        uint64_t t749 = (uintptr_t)t748;
                                        uint64_t t750 = gm_l66;
                                        uint64_t r751; bool o752 = __builtin_add_overflow(t750, ((uint64_t)1ULL), &r751);
                                        if (o752) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 101, 83);
                                        uint64_t t753 = r751;
                                        uint64_t t754 = gm_l61;
                                        uint64_t r755; bool o756 = __builtin_mul_overflow(t753, t754, &r755);
                                        if (o756) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 101, 82);
                                        uint64_t t757 = r755;
                                        uint64_t t758 = (uintptr_t)t757;
                                        if (t749 > t758 || t758 > (uintptr_t)t742) gm_trap_slice(t749, t758, t742, gm_srcns60k96jj1mt, 101, 68);
                                        gm_b16_8 t759;
                                        GM_ST(void *, (uint8_t *)&t759, (void *)(((uint8_t *)t743 + t749 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t759 + 8, (uintptr_t)(t758 - t749));
                                        gm_b16_8 t760 = GM_LD(gm_b16_8, ((uint8_t *)&t759));
                                        uint64_t t761 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                        void * t762 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                                        uint64_t t763 = gm_l66;
                                        uint64_t t764 = gm_l62;
                                        uint64_t r765; bool o766 = __builtin_mul_overflow(t763, t764, &r765);
                                        if (o766) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 101, 102);
                                        uint64_t t767 = r765;
                                        uint64_t t768 = (uintptr_t)t767;
                                        uint64_t t769 = gm_l66;
                                        uint64_t r770; bool o771 = __builtin_add_overflow(t769, ((uint64_t)1ULL), &r770);
                                        if (o771) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 101, 112);
                                        uint64_t t772 = r770;
                                        uint64_t t773 = gm_l62;
                                        uint64_t r774; bool o775 = __builtin_mul_overflow(t772, t773, &r774);
                                        if (o775) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 101, 111);
                                        uint64_t t776 = r774;
                                        uint64_t t777 = (uintptr_t)t776;
                                        if (t768 > t777 || t777 > (uintptr_t)t761) gm_trap_slice(t768, t777, t761, gm_srcns60k96jj1mt, 101, 98);
                                        gm_b16_8 t778;
                                        GM_ST(void *, (uint8_t *)&t778, (void *)(((uint8_t *)t762 + t768 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t778 + 8, (uintptr_t)(t777 - t768));
                                        gm_tcfa6l7reoosn t779;
                                        gm_f_convert_is6an2k((void *)&t779, (void *)NULL, cx, NULL, t735, t736, t737, t738, t739, t760, t778);
                                        if (gm_unw.kind) {
                                            goto gm_ll728;
                                        }
                                        if (((((uint8_t *)&t779))[0] != 0)) {
                                            gm_tcfa6l7reoosn t780;
                                            GM_ST(uint8_t, (uint8_t *)&t780 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t780 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t780 + 4, 0);
                                            ((uint8_t *)&t780)[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t780 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t779) + 8)));
                                            F.ret = t780;
                                            goto gm_fret;
                                        }
                                        gm_b0_1 t781 = GM_LD(gm_b0_1, (((uint8_t *)&t779) + 8));
                                    }
                                    goto me731;
mn732:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 101, 13);
me731:;
                                }
lc724:;
                                {
                                    uint64_t t783 = gm_l65;
                                    uint64_t r784; bool o785 = __builtin_add_overflow(t783, ((uint64_t)1ULL), &r784);
                                    uint64_t t786 = r784;
                                    gm_l65 = t786;
                                }
                            }
gm_ll728:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 8) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb724; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc724; }
                            }
                            goto gm_lad;
lb724:;
                        }
                    }
                    goto ix690;
ie690:;
                    {
                        void * t788 = gm_l0;
                        gm_tl874t2atnlh3 t789 = gm_l1;
                        gm_b16_8 t790;
                        GM_ST(void *, (uint8_t *)&t790, (void *)(gm_smsrao7h22muv));
                        GM_ST(uintptr_t, (uint8_t *)&t790 + 8, (uintptr_t)(60));
                        gm_t76123gfet73g t791;
                        gm_f_trap_qh84l9d((void *)&t791, (void *)NULL, cx, NULL, t788, t789, t790);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t791);
                        goto gm_fdone;
                    }
ix690:;
                }
                goto ix682;
ie682:;
                {
                    void * t793 = gm_l0;
                    gm_tl874t2atnlh3 t794 = gm_l1;
                    gm_b16_8 t795;
                    GM_ST(void *, (uint8_t *)&t795, (void *)(gm_spnr4ftjbm9m9));
                    GM_ST(uintptr_t, (uint8_t *)&t795 + 8, (uintptr_t)(60));
                    gm_t76123gfet73g t796;
                    gm_f_trap_qh84l9d((void *)&t796, (void *)NULL, cx, NULL, t793, t794, t795);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t796);
                    goto gm_fdone;
                }
ix682:;
            }
            goto me3;
mn679:;
ma12:;
            uint8_t t798 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t798 != ((uint8_t)57ULL)) goto mn797;
            {
                void * t801 = gm_l0;
                gm_t88rcucg1b7uk t802 = gm_l4;
                gm_t53at80pfdus3 t803;
                gm_f_kind_htv6093((void *)&t803, t801, GM_CONV(gm_csguankko4lmhd, t802));
                uint8_t t804 = GM_LD(uint8_t, (((uint8_t *)&t803) + 0));
                if (t804 != ((uint8_t)11ULL)) goto ie800;
                uint64_t t805 = GM_LD(uint64_t, ((((uint8_t *)&t803) + 8) + 0));
                gm_l67 = t805;
                gm_t88rcucg1b7uk t806 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t803) + 8) + 8));
                gm_l68 = t806;
                {
                    void * t808 = gm_l0;
                    gm_t88rcucg1b7uk t809 = gm_l68;
                    uint64_t t810 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t808, t809);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t811 = gm_f_us_cuv01fp(t810);
                    gm_l69 = t811;
                }
                {
                    {
                        gm_l71 = ((uint64_t)1ULL);
                    }
                    {
                        uint64_t t813 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_l70 = t813;
                    }
                    {
                        for (;;) {
                            uint64_t t815 = gm_l71;
                            uint64_t t816 = gm_l70;
                            uint8_t t817 = (t815 < t816);
                            if (!t817) goto lb814;
                            {
                                uint64_t t820 = gm_l71;
                                uint64_t t823 = GM_LD(uint64_t, ((uint8_t *)&t820));
                                gm_l72 = t823;
                                {
                                    void * t825 = gm_l0;
                                    uint64_t t826 = gm_l72;
                                    uint64_t t827 = (uintptr_t)t826;
                                    uint64_t t828 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    if (t827 >= (uintptr_t)(t828)) gm_trap_index(t827, t828, gm_srcns60k96jj1mt, 107, 35);
                                    gm_b16_8 t829 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t827 * 16));
                                    gm_b16_8 t830 = GM_LD(gm_b16_8, ((uint8_t *)&t829));
                                    uint64_t t831 = gm_f_get_u64_tgpb2vh(t825, t830);
                                    uint64_t t832 = gm_f_us_cuv01fp(t831);
                                    gm_l73 = t832;
                                }
                                {
                                    uint64_t t834 = gm_l73;
                                    uint64_t t835 = gm_l67;
                                    uint64_t t836 = gm_f_us_cuv01fp(t835);
                                    uint8_t t837 = (t834 >= t836);
                                    if (!t837) goto ie833;
                                    {
                                        void * t839 = gm_l0;
                                        gm_tl874t2atnlh3 t840 = gm_l1;
                                        gm_b16_8 t841;
                                        GM_ST(void *, (uint8_t *)&t841, (void *)(gm_ssvci9220kuoj));
                                        GM_ST(uintptr_t, (uint8_t *)&t841 + 8, (uintptr_t)(44));
                                        gm_t76123gfet73g t842;
                                        gm_f_trap_qh84l9d((void *)&t842, (void *)NULL, cx, NULL, t839, t840, t841);
                                        if (gm_unw.kind) {
                                            goto gm_ll818;
                                        }
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t842);
                                        goto gm_fdone;
                                    }
                                    goto ix833;
ie833:;
ix833:;
                                }
                                {
                                    uint64_t t843 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    void * t844 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                                    uint64_t t845 = gm_l72;
                                    uint64_t r846; bool o847 = __builtin_sub_overflow(t845, ((uint64_t)1ULL), &r846);
                                    if (o847) gm_trap("integer overflow in `-`", gm_srcns60k96jj1mt, 109, 22);
                                    uint64_t t848 = r846;
                                    uint64_t t849 = gm_l69;
                                    uint64_t r850; bool o851 = __builtin_mul_overflow(t848, t849, &r850);
                                    if (o851) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 109, 21);
                                    uint64_t t852 = r850;
                                    uint64_t t853 = (uintptr_t)t852;
                                    uint64_t t854 = gm_l72;
                                    uint64_t t855 = gm_l69;
                                    uint64_t r856; bool o857 = __builtin_mul_overflow(t854, t855, &r856);
                                    if (o857) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 109, 35);
                                    uint64_t t858 = r856;
                                    uint64_t t859 = (uintptr_t)t858;
                                    if (t853 > t859 || t859 > (uintptr_t)t843) gm_trap_slice(t853, t859, t843, gm_srcns60k96jj1mt, 109, 17);
                                    gm_b16_8 t860;
                                    GM_ST(void *, (uint8_t *)&t860, (void *)(((uint8_t *)t844 + t853 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t860 + 8, (uintptr_t)(t859 - t853));
                                    uint64_t t861 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t862 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    if (t861 >= (uintptr_t)(t862)) gm_trap_index(t861, t862, gm_srcns60k96jj1mt, 109, 53);
                                    uint64_t t863 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t861 * 16) + 8);
                                    void * t864 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t861 * 16)));
                                    uint64_t t865 = gm_l73;
                                    uint64_t t866 = gm_l69;
                                    uint64_t r867; bool o868 = __builtin_mul_overflow(t865, t866, &r867);
                                    if (o868) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 109, 58);
                                    uint64_t t869 = r867;
                                    uint64_t t870 = (uintptr_t)t869;
                                    uint64_t t871 = gm_l73;
                                    uint64_t r872; bool o873 = __builtin_add_overflow(t871, ((uint64_t)1ULL), &r872);
                                    if (o873) gm_trap("integer overflow in `+`", gm_srcns60k96jj1mt, 109, 67);
                                    uint64_t t874 = r872;
                                    uint64_t t875 = gm_l69;
                                    uint64_t r876; bool o877 = __builtin_mul_overflow(t874, t875, &r876);
                                    if (o877) gm_trap("integer overflow in `*`", gm_srcns60k96jj1mt, 109, 66);
                                    uint64_t t878 = r876;
                                    uint64_t t879 = (uintptr_t)t878;
                                    if (t870 > t879 || t879 > (uintptr_t)t863) gm_trap_slice(t870, t879, t863, gm_srcns60k96jj1mt, 109, 53);
                                    gm_b16_8 t880;
                                    GM_ST(void *, (uint8_t *)&t880, (void *)(((uint8_t *)t864 + t870 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t880 + 8, (uintptr_t)(t879 - t870));
                                    gm_b16_8 t881 = GM_LD(gm_b16_8, ((uint8_t *)&t880));
                                    gm_f_copy_from_f7j6p05(t860, t881);
                                }
                                goto me821;
mn822:;
                                gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 106, 13);
me821:;
                            }
lc814:;
                            {
                                uint64_t t883 = gm_l71;
                                uint64_t r884; bool o885 = __builtin_add_overflow(t883, ((uint64_t)1ULL), &r884);
                                uint64_t t886 = r884;
                                gm_l71 = t886;
                            }
                        }
gm_ll818:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 9) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb814; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc814; }
                        }
                        goto gm_lad;
lb814:;
                    }
                }
                goto ix800;
ie800:;
                {
                    void * t888 = gm_l0;
                    gm_tl874t2atnlh3 t889 = gm_l1;
                    gm_b16_8 t890;
                    GM_ST(void *, (uint8_t *)&t890, (void *)(gm_s1tv0n9hk7497));
                    GM_ST(uintptr_t, (uint8_t *)&t890 + 8, (uintptr_t)(57));
                    gm_t76123gfet73g t891;
                    gm_f_trap_qh84l9d((void *)&t891, (void *)NULL, cx, NULL, t888, t889, t890);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t891);
                    goto gm_fdone;
                }
ix800:;
            }
            goto me3;
mn797:;
ma13:;
            void * t893 = gm_l0;
            gm_tl874t2atnlh3 t894 = gm_l1;
            gm_b16_8 t895;
            GM_ST(void *, (uint8_t *)&t895, (void *)(gm_s5e5797b1jurs));
            GM_ST(uintptr_t, (uint8_t *)&t895 + 8, (uintptr_t)(19));
            gm_t76123gfet73g t896;
            gm_f_unsupported_70vvk1e((void *)&t896, (void *)NULL, cx, NULL, t893, t894, t895);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t896);
            goto gm_fdone;
            goto me3;
mn892:;
ma14:;
            gm_trap("no match arm matched the scrutinee", gm_srcns60k96jj1mt, 7, 5);
me3:;
        }
    }
    gm_b0_1 t897;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t897);
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
