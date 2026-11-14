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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t pe[5]; } gm_t81j4cfpg2pf9;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; void * f3; } gm_tg0sdqqpruvqq;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint64_t f0; uint64_t f1; uint32_t f2; uint8_t p3[4]; uint64_t f3; } gm_tnen6863q2sjs;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct gm_csqm5vmbpo6ge8 { uint32_t f0; uint32_t f1; gm_csguankko4lmhd f2; gm_cseahvg8v7p0vs f3; } gm_csqm5vmbpo6ge8;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_t16on0i8enr1i;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_t5g5vgung6h6s;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t81j4cfpg2pf9) == 16 && _Alignof(gm_t81j4cfpg2pf9) == 8 && offsetof(gm_t81j4cfpg2pf9, f0) == 0 && offsetof(gm_t81j4cfpg2pf9, f1) == 8 && offsetof(gm_t81j4cfpg2pf9, f2) == 9 && offsetof(gm_t81j4cfpg2pf9, f3) == 10, "layout of `Open`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tg0sdqqpruvqq) == 32 && _Alignof(gm_tg0sdqqpruvqq) == 8 && offsetof(gm_tg0sdqqpruvqq, f0) == 0 && offsetof(gm_tg0sdqqpruvqq, f1) == 8 && offsetof(gm_tg0sdqqpruvqq, f2) == 16 && offsetof(gm_tg0sdqqpruvqq, f3) == 24, "layout of `(local: u32, off: u64, size: u64, init: ?^Node)`");
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; gm_tl874t2atnlh3 f3; } gm_tgk5j0pob2pqa;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tnen6863q2sjs) == 32 && _Alignof(gm_tnen6863q2sjs) == 8 && offsetof(gm_tnen6863q2sjs, f0) == 0 && offsetof(gm_tnen6863q2sjs, f1) == 8 && offsetof(gm_tnen6863q2sjs, f2) == 16 && offsetof(gm_tnen6863q2sjs, f3) == 24, "layout of `(usize, usize, u32, usize)`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t81j4cfpg2pf9 f0; uint8_t f1; uint8_t pe[7]; } gm_toql92ug1137o;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; uint64_t f4; uint64_t f5; } gm_tr4ummvbupakb;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_csqm5vmbpo6ge8) == 24 && _Alignof(gm_csqm5vmbpo6ge8) == 4 && offsetof(gm_csqm5vmbpo6ge8, f0) == 0 && offsetof(gm_csqm5vmbpo6ge8, f1) == 4 && offsetof(gm_csqm5vmbpo6ge8, f2) == 8 && offsetof(gm_csqm5vmbpo6ge8, f3) == 12, "layout of `LabelBreak`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t16on0i8enr1i) == 32 && _Alignof(gm_t16on0i8enr1i) == 8 && offsetof(gm_t16on0i8enr1i, f0) == 0 && offsetof(gm_t16on0i8enr1i, f1) == 16 && offsetof(gm_t16on0i8enr1i, f2) == 28, "layout of `LabelUse`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tg0sdqqpruvqq v0; gm_tpe1hv3fekn05 v1; gm_ttggq7ng4kp3p v2; gm_t3mf58gv9kf3n v3; gm_tpe1hv3fekn05 v4; void * v5; gm_tgtub0v94d5u5 v6; } f1; } gm_t2k9dbsv5ff58;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t5g5vgung6h6s) == 32 && _Alignof(gm_t5g5vgung6h6s) == 8 && offsetof(gm_t5g5vgung6h6s, f0) == 0 && offsetof(gm_t5g5vgung6h6s, f1) == 8 && offsetof(gm_t5g5vgung6h6s, f2) == 24, "layout of `(u32, []u8, u32)`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgk5j0pob2pqa) == 24 && _Alignof(gm_tgk5j0pob2pqa) == 4 && offsetof(gm_tgk5j0pob2pqa, f0) == 0 && offsetof(gm_tgk5j0pob2pqa, f1) == 4 && offsetof(gm_tgk5j0pob2pqa, f2) == 8 && offsetof(gm_tgk5j0pob2pqa, f3) == 12, "layout of `LabelBreak`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toql92ug1137o) == 24 && _Alignof(gm_toql92ug1137o) == 8 && offsetof(gm_toql92ug1137o, f0) == 0 && offsetof(gm_toql92ug1137o, f1) == 16, "layout of `?Open`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_tr4ummvbupakb) == 64 && _Alignof(gm_tr4ummvbupakb) == 8 && offsetof(gm_tr4ummvbupakb, f0) == 0 && offsetof(gm_tr4ummvbupakb, f1) == 16 && offsetof(gm_tr4ummvbupakb, f2) == 24 && offsetof(gm_tr4ummvbupakb, f3) == 32 && offsetof(gm_tr4ummvbupakb, f4) == 48 && offsetof(gm_tr4ummvbupakb, f5) == 56, "layout of `(string, u64, u64, string, u64, u64)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t2k9dbsv5ff58) == 40 && _Alignof(gm_t2k9dbsv5ff58) == 8 && offsetof(gm_t2k9dbsv5ff58, f0) == 0 && offsetof(gm_t2k9dbsv5ff58, f1) == 8, "layout of `St`");
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { uint64_t f0; gm_tvpnktv5fo8nl f1; } gm_tp8h583g7s5hd;
typedef struct { gm_t0d02142k4tu8 f0; void * f1; } gm_tuqaojgkqdcop;
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_tp8h583g7s5hd) == 168 && _Alignof(gm_tp8h583g7s5hd) == 8 && offsetof(gm_tp8h583g7s5hd, f0) == 0 && offsetof(gm_tp8h583g7s5hd, f1) == 8, "layout of `OtherItem`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuqaojgkqdcop v0; void * v1; } f1; } gm_tq5j0547p8452;
_Static_assert(sizeof(gm_tuqaojgkqdcop) == 72 && _Alignof(gm_tuqaojgkqdcop) == 8 && offsetof(gm_tuqaojgkqdcop, f0) == 0 && offsetof(gm_tuqaojgkqdcop, f1) == 64, "layout of `(pat: Pattern, value: ^Expr)`");
_Static_assert(sizeof(gm_tq5j0547p8452) == 80 && _Alignof(gm_tq5j0547p8452) == 8 && offsetof(gm_tq5j0547p8452, f0) == 0 && offsetof(gm_tq5j0547p8452, f1) == 8, "layout of `CondPart`");
struct gm_fr_clear_ufjejrf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_e8lr7h0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_ectdopm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_ectdopm_2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_u4v62un {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_30da7i6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6ld1tun {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73gbl6u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_760cgr0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7gkjmge {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d0lhrvs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_esq6vjf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f1ngfj0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hmpovae {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_i0ticdn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_icu3qs9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ipqblac {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jjs3scl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_l8m0eod {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lv6i4jh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lv6i4jh_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_m3oe7t3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n5n7pq1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oldiiq9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oldiiq9_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p5lo8ed {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p5lo8ed_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p8ki924 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_plluplu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rtces00 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rtces00_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uq9fi1n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vefkkhp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_e1j40m3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_ehh2367 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_eht4gub {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_05foihn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_097u1ss {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1qdv1d5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3de08l2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3if10s4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_47323no {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6ms6csh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9sjif99 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_co27i32 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_d9c49u0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dkb05ql {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_f60m3fk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fsudkd6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_jotf1n0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_l7nrntj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_l80efih {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ll0fd0a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mj05e1r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_n1ut9t3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_n85ptlm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_npkjc6s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_o5t93u5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_qffm308 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_sar6mkr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_sd7157p {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ucp1n4i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_insert_u31m55g {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_into_slice_e14ea2s {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_list_e6ie1fq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_e6ie1fq_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_eb5ua5r {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_eh2bcst {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_u1fokev {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uaa66su {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uahmact {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ubba23m {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ubba23m_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uc1atd0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ue2t2bm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_umenr17 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_urufktd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uun92du {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_pop_u3dhp9e {
    gm_frame h;
    gm_toql92ug1137o ret;
};
struct gm_fr_push_eb7kbhp {
    gm_frame h;
    gm_tq5j0547p8452 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_eb7kbhp_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ehvjfcg {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ej78v89 {
    gm_frame h;
    gm_tk9n3vlila9qg l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_em9e1kh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_esf6o94 {
    gm_frame h;
    gm_t2k9dbsv5ff58 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u0rp7b1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u2jka7o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u2jka7o_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u46i39f {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u4kbe6d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u9lban8 {
    gm_frame h;
    gm_tp8h583g7s5hd l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_uehemid {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_usak761 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_1dn6qmo {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3iuca1r {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4bta8ne {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4bta8ne_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_78ppoki {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_94j8fj1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_94j8fj1_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_967kt0a_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_e4d8f0p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_e5frfkp {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_edp3chb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_efufsip {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_egu240o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_emhp9ia {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_eml2a4s {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_emnjbt3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_f2uhbto {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_nd6prsl {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_our9l6p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_pcenrov {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_q7kpg21 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_r8ccoha {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_r9l0iqd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_t4b6ipk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_u824ov8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_u824ov8_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ub69oip {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_urfcki5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_urfcki5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_usnqfd3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_vlhhu7d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_truncate_eoppkeo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_ua7l21h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_ucmmehv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_e15chue {
    gm_frame h;
    void * l0; /* l */
    gm_tr4ummvbupakb l1; /* args */
    uint8_t l2; /* failed */
    gm_tr4ummvbupakb l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_e9824eu {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ed06dp1 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ef6g7s0 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_epd64m2 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_erog63d {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u0r3vc4 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u1no4ia {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u3br5ci {
    gm_frame h;
    void * l0; /* l */
    gm_tud7oovfvnf43 l1; /* args */
    uint8_t l2; /* failed */
    gm_tud7oovfvnf43 l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u4hj8pn {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u50pgu2 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u5ur1qg {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u8qima9 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u8u1amh {
    gm_frame h;
    void * l0; /* l */
    gm_tfeab279c16rr l1; /* args */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u9o0juu {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ud12ae8 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ue93873 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_uebhmoa {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_uf08lpv {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ufh35j8 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ug67q96 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_upbce2g {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_upr5k5n {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_uqdv1oa {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_usci3o4 {
    gm_frame h;
    void * l0; /* l */
    uint8_t l2; /* failed */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_uuia5cm {
    gm_frame h;
    void * l0; /* l */
    gm_tq3m5qgvkpje8 l1; /* args */
    uint8_t l2; /* failed */
    gm_tq3m5qgvkpje8 l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
inline __attribute__((aligned(4))) void gm_f_clear_ufjejrf(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_e8lr7h0(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_ectdopm(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_ectdopm_2(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_fac02bd(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_u4v62un(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_30da7i6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6ld1tun(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73gbl6u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_760cgr0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7gkjmge(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0lhrvs(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_esq6vjf(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f1ngfj0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hmpovae(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_i0ticdn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_icu3qs9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ipqblac(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jjs3scl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_l8m0eod(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lv6i4jh(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lv6i4jh_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_m3oe7t3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5n7pq1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oldiiq9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oldiiq9_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p8ki924(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_plluplu(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uq9fi1n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vefkkhp(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_e1j40m3(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_ehh2367(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_eht4gub(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05foihn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_097u1ss(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1qdv1d5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3de08l2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3if10s4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_47323no(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6ms6csh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9sjif99(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_co27i32(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_d9c49u0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dkb05ql(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f60m3fk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fsudkd6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jotf1n0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l7nrntj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l80efih(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ll0fd0a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mj05e1r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n1ut9t3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n85ptlm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_npkjc6s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o5t93u5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qffm308(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sar6mkr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sd7157p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ucp1n4i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_insert_u31m55g(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_into_slice_e14ea2s(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tabb710d9mq2e gm_f_into_slice_e14ea2s_c(void * a0);
inline __attribute__((aligned(4))) void gm_f_list_e6ie1fq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_e6ie1fq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_eb5ua5r(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_eh2bcst(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_u1fokev(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uaa66su(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uahmact(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ubba23m(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ubba23m_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uc1atd0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ue2t2bm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_umenr17(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_urufktd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uun92du(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_u3dhp9e(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_toql92ug1137o gm_f_pop_u3dhp9e_c(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp_2_c(void * a0, gm_tlr9dp305htm7 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp_c(void * a0, gm_tq5j0547p8452 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ehvjfcg(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ehvjfcg_c(void * a0, gm_t3ujhu6qph7de a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ej78v89(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ej78v89_c(void * a0, gm_tk9n3vlila9qg a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_em9e1kh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_em9e1kh_c(void * a0, gm_t16on0i8enr1i a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_esf6o94(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_esf6o94_c(void * a0, gm_t2k9dbsv5ff58 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u0rp7b1(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u2jka7o(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u2jka7o_2(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u46i39f(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u46i39f_c(void * a0, gm_trqfvmjtt4sij a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u4kbe6d(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u4kbe6d_c(void * a0, gm_t5g5vgung6h6s a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u9lban8(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u9lban8_c(void * a0, gm_tp8h583g7s5hd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_uehemid(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_usak761(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_usak761_c(void * a0, gm_csqm5vmbpo6ge8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1dn6qmo(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3iuca1r(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_78ppoki(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_94j8fj1(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_94j8fj1_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_967kt0a_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_e4d8f0p(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_e5frfkp(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_edp3chb(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_efufsip(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_egu240o(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emhp9ia(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_eml2a4s(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_f2uhbto(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_nd6prsl(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_our9l6p(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_pcenrov(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q7kpg21(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r8ccoha(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r9l0iqd(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t4b6ipk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_u824ov8(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_u824ov8_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ub69oip(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_urfcki5(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_urfcki5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_usnqfd3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vlhhu7d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_m6out5p(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_eoppkeo(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_ua7l21h(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_ucmmehv(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e15chue(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e15chue_c(void * a0, gm_tr4ummvbupakb a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e9824eu(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ed06dp1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ed06dp1_c(void * a0, gm_tf1bjg81u0glv a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ef6g7s0(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_epd64m2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_epd64m2_c(void * a0, gm_t6j6smj7qoctu a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_erog63d(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u0r3vc4(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u0r3vc4_c(void * a0, gm_t5hdnnftkup6m a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u1no4ia(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u1no4ia_c(void * a0, gm_t3877pk7lv748 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u3br5ci(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u3br5ci_c(void * a0, gm_tud7oovfvnf43 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u4hj8pn(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u50pgu2(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u5ur1qg(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u8qima9(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u8u1amh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u8u1amh_c(void * a0, gm_tfeab279c16rr a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u9o0juu(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u9o0juu_c(void * a0, gm_t6j6smj7qoctu a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ud12ae8(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ue93873(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ue93873_c(void * a0, gm_t6j6smj7qoctu a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uebhmoa(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uf08lpv(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uf08lpv_c(void * a0, gm_t6j6smj7qoctu a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ufh35j8(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ufh35j8_c(void * a0, gm_t6j6smj7qoctu a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ug67q96(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ug67q96_c(void * a0, gm_tnen6863q2sjs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_upbce2g(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_upr5k5n(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uqdv1oa(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_usci3o4(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_usci3o4_c(void * a0, gm_t6j6smj7qoctu a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uuia5cm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uuia5cm_c(void * a0, gm_tq3m5qgvkpje8 a1);
GM_NOINLINE bool gm_pf0aqhubr4up(void *code, void *env, gm_node *cx, const void *d, uintptr_t n);
GM_NOINLINE bool gm_pfk8csn7us23(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s);
extern const uint8_t gm_s0fim1krogkdi[18];
extern const uint8_t gm_s0jrrhhm4skoj[15];
extern const uint8_t gm_s16mf4cvajh59[13];
extern const uint8_t gm_s1d673tn03i3r[5];
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s2kl2d59c04nf[19];
extern const uint8_t gm_s3347hvco05di[6];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s48iukd74s6c6[22];
extern const uint8_t gm_s4dun9lv4h66e[4];
extern const uint8_t gm_s4l2s2smm9apo[21];
extern const uint8_t gm_s52fbscfsv3pa[6];
extern const uint8_t gm_s5vogfeoqmvr7[4];
extern const uint8_t gm_s6t4r831mukdr[3];
extern const uint8_t gm_s77fi68iu5q6n[5];
extern const uint8_t gm_s8a7jvbqoerpn[21];
extern const uint8_t gm_s8lpasdulkmbv[28];
extern const uint8_t gm_s97nfli622uqg[5];
extern const uint8_t gm_s9md3pg6vlt5h[3];
extern const uint8_t gm_s9sev457i3j7n[5];
extern const uint8_t gm_sabu5uehm65n2[22];
extern const uint8_t gm_sasea0sobfkrs[8];
extern const uint8_t gm_sbnuvhnotkfkr[5];
extern const uint8_t gm_sc8o4oaph9gbr[2];
extern const uint8_t gm_scedkbh0j6d78[6];
extern const uint8_t gm_sda1bf43v6985[10];
extern const uint8_t gm_sdhelph0q09tv[5];
extern const uint8_t gm_sdmle2qsm85vh[11];
extern const uint8_t gm_se47039ecu2b8[10];
extern const uint8_t gm_sf4qi8ut6u3p1[5];
extern const uint8_t gm_sg398rgmck67b[4];
extern const uint8_t gm_sg5e6vqb8nie3[18];
extern const uint8_t gm_sgosg5fctcl72[17];
extern const uint8_t gm_sgq8hfs47hogb[9];
extern const uint8_t gm_sgtmc08t3k49t[42];
extern const uint8_t gm_shakeh6dfb84i[17];
extern const uint8_t gm_shc0s7vbfquc0[20];
extern const uint8_t gm_shmpls8n0g8h9[4];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sjbjq95a18dio[44];
extern const uint8_t gm_sjs62u07kjioq[15];
extern const uint8_t gm_sk754mj3p1el6[2];
extern const uint8_t gm_skfdo9qggtrbp[6];
extern const uint8_t gm_skpgi6v1lj8m9[19];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_snaenr23tbgmb[23];
extern const uint8_t gm_snr5o5tak6405[4];
extern const uint8_t gm_snvldulppvqal[23];
extern const uint8_t gm_snvodad5t43r7[11];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so7oqi7vob9ku[17];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_sp1nce97af2b9[6];
extern const uint8_t gm_spbhhmdbp335u[4];
extern const uint8_t gm_spj9rva8kb5ar[3];
extern const uint8_t gm_sqrh6i8djv6b4[7];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ss8lvrvkhracl[21];
extern const uint8_t gm_ssfg877iegnma[4];
extern const uint8_t gm_stb42390ups9k[6];
extern const uint8_t gm_svgje5djlutuu[25];
extern const uint8_t gm_svsf0pu3t8u6m[7];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_tyb2nno7n67r;
extern const gm_type gm_tybihgc69jv3;
inline __attribute__((aligned(4))) void gm_f_clear_ufjejrf(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_ufjejrf F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        void * t5 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t3) + 0)));
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 137, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_30da7i6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_30da7i6 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 168)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5nn7ph0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6ld1tun(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6ld1tun F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73gbl6u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73gbl6u F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_760cgr0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_760cgr0 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7gkjmge(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7gkjmge F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0lhrvs(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d0lhrvs F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi_3 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_esq6vjf(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_esq6vjf F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f1ngfj0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f1ngfj0 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hmpovae(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hmpovae F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_i0ticdn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_i0ticdn F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_icu3qs9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_icu3qs9 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ijtp9lo F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)1ULL));
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 1)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ipqblac(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ipqblac F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 216)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jjs3scl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jjs3scl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_l8m0eod(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_l8m0eod F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lmbdkhb F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lmbdkhb_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lv6i4jh(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lv6i4jh F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 104)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lv6i4jh_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lv6i4jh_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_m3oe7t3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_m3oe7t3 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5n7pq1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n5n7pq1 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oldiiq9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oldiiq9 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oldiiq9_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oldiiq9_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p5lo8ed F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p5lo8ed_3 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p8ki924(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p8ki924 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_plluplu(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_plluplu F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rtces00 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rtces00_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uq9fi1n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uq9fi1n F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vefkkhp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vefkkhp F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05foihn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_05foihn F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_u8qima9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_u8qima9 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_u8qima9 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_097u1ss(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_097u1ss F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_usci3o4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_usci3o4 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_usci3o4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1qdv1d5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1qdv1d5 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_uebhmoa *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_uebhmoa *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_uebhmoa *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3de08l2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_3de08l2 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_e9824eu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_e9824eu *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_e9824eu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3if10s4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_3if10s4 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_upr5k5n *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_upr5k5n *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_upr5k5n *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_47323no(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_47323no F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_uqdv1oa *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_uqdv1oa *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_uqdv1oa *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6ms6csh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_6ms6csh F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_ef6g7s0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_ef6g7s0 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_ef6g7s0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9sjif99(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9sjif99 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_erog63d *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_erog63d *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_erog63d *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_co27i32(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_co27i32 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_ud12ae8 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_ud12ae8 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_ud12ae8 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_d9c49u0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_d9c49u0 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_u3br5ci *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_u3br5ci *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_u3br5ci *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dkb05ql(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_dkb05ql F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_epd64m2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_epd64m2 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_epd64m2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f60m3fk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_f60m3fk F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_u50pgu2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_u50pgu2 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_u50pgu2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fsudkd6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fsudkd6 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_ue93873 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_ue93873 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_ue93873 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jotf1n0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_jotf1n0 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_ufh35j8 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_ufh35j8 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_ufh35j8 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l7nrntj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_l7nrntj F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_u0r3vc4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_u0r3vc4 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_u0r3vc4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l80efih(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_l80efih F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_e15chue *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_e15chue *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_e15chue *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ll0fd0a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ll0fd0a F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_u1no4ia *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_u1no4ia *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_u1no4ia *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mj05e1r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_mj05e1r F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_u5ur1qg *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_u5ur1qg *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_u5ur1qg *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n1ut9t3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_n1ut9t3 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_uf08lpv *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_uf08lpv *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_uf08lpv *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n85ptlm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_n85ptlm F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_u8u1amh *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_u8u1amh *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_u8u1amh *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_npkjc6s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_npkjc6s F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_uuia5cm *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_uuia5cm *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_uuia5cm *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o5t93u5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_o5t93u5 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_ug67q96 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_ug67q96 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_ug67q96 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qffm308(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_qffm308 F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_ed06dp1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_ed06dp1 *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_ed06dp1 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sar6mkr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_sar6mkr F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_u9o0juu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_u9o0juu *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_u9o0juu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sd7157p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_sd7157p F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_upbce2g *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_upbce2g *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_upbce2g *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ucp1n4i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ucp1n4i F;
    gm_b16_8 gm_l0; /* b */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_write_u4hj8pn *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_u4hj8pn *)((&F.h)->up))->l0));
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_write_u4hj8pn *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_e6ie1fq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_e6ie1fq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_oldiiq9();
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
inline __attribute__((aligned(4))) void gm_f_list_e6ie1fq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_e6ie1fq_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_oldiiq9_2();
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
inline __attribute__((aligned(4))) void gm_f_list_eb5ua5r(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_eb5ua5r F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ipqblac();
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
inline __attribute__((aligned(4))) void gm_f_list_eh2bcst(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_eh2bcst F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_760cgr0();
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
inline __attribute__((aligned(4))) void gm_f_list_u1fokev(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_u1fokev F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_esq6vjf();
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
inline __attribute__((aligned(4))) void gm_f_list_uaa66su(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uaa66su F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_n5n7pq1();
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
inline __attribute__((aligned(4))) void gm_f_list_uahmact(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uahmact F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_l8m0eod();
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
inline __attribute__((aligned(4))) void gm_f_list_ubba23m(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ubba23m F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lv6i4jh();
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
inline __attribute__((aligned(4))) void gm_f_list_ubba23m_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ubba23m_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lv6i4jh_2();
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
inline __attribute__((aligned(4))) void gm_f_list_uc1atd0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uc1atd0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_6ld1tun();
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
inline __attribute__((aligned(4))) void gm_f_list_ue2t2bm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ue2t2bm F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_d0lhrvs();
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
inline __attribute__((aligned(4))) void gm_f_list_umenr17(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_umenr17 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_plluplu();
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
inline __attribute__((aligned(4))) void gm_f_list_urufktd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_urufktd F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_i0ticdn();
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
inline __attribute__((aligned(4))) void gm_f_list_uun92du(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uun92du F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_uq9fi1n();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_u3dhp9e(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_u3dhp9e F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t81j4cfpg2pf9 gm_l2; /* x */
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
            gm_toql92ug1137o t8;
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
        gm_t81j4cfpg2pf9 t17 = GM_LD(gm_t81j4cfpg2pf9, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 16));
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_t81j4cfpg2pf9 t29 = gm_l2;
        gm_toql92ug1137o t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 20, 0);
        GM_ST(gm_t81j4cfpg2pf9, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[16] = 1;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1dn6qmo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_1dn6qmo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3iuca1r(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3iuca1r F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)168ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4bta8ne F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)80ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4bta8ne_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_78ppoki(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_78ppoki F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)224ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_94j8fj1(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_94j8fj1 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_94j8fj1_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_94j8fj1_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_967kt0a_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_967kt0a_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
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
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
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
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_edp3chb(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_edp3chb F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_efufsip(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_efufsip F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_egu240o(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_egu240o F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emhp9ia(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_emhp9ia F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)20ULL), ((uint64_t)4ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_eml2a4s(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_eml2a4s F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)96ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_emnjbt3 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_f2uhbto(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_f2uhbto F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
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
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_nd6prsl(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_nd6prsl F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_our9l6p(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_our9l6p F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)4ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_pcenrov(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_pcenrov F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q7kpg21(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_q7kpg21 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r8ccoha(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_r8ccoha F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r9l0iqd(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_r9l0iqd F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t4b6ipk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_t4b6ipk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_u824ov8(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_u824ov8 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)4ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_u824ov8_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_u824ov8_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)4ULL));
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ub69oip(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ub69oip F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_urfcki5(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_urfcki5 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_urfcki5_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_urfcki5_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_usnqfd3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_usnqfd3 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_vlhhu7d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_vlhhu7d F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 89u);
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
        gm_tftb1odc739t5 t9 = t8;
        uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_truncate_eoppkeo(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_eoppkeo F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 4)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_ua7l21h(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_ua7l21h F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 40)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_ucmmehv(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_ucmmehv F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 8)));
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
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_upeumbt F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 224)));
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
extern __attribute__((aligned(4))) void gm_f_clear_ufjejrf(void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_e8lr7h0(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_e8lr7h0 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 16));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, ((((uint8_t *)t7) + 24) + 8));
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 16));
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, ((((uint8_t *)t10) + 24) + 0));
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            void * t14 = t13;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 16));
            uint64_t r17; bool o18 = __builtin_mul_overflow(((uint64_t)32ULL), t16, &r17);
            if (o18) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 143, 64);
            uint64_t t19 = r17;
            void * t20 = t9;
            gm_b0_1 t21;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t20))(t11, t14, t19, ((uint64_t)8ULL));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l0;
        uint8_t *t23 = (((uint8_t *)t22) + 0);
        gm_b16_8 t24 = gm_f_empty_slice_7gkjmge();
        GM_ST(gm_b16_8, t23, t24);
    }
    {
        void * t25 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t25) + 16), ((uint64_t)0ULL));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_ectdopm(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_ectdopm F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 16));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, ((((uint8_t *)t7) + 24) + 8));
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 16));
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, ((((uint8_t *)t10) + 24) + 0));
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            void * t14 = t13;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 16));
            uint64_t r17; bool o18 = __builtin_mul_overflow(((uint64_t)16ULL), t16, &r17);
            if (o18) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 143, 64);
            uint64_t t19 = r17;
            void * t20 = t9;
            gm_b0_1 t21;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t20))(t11, t14, t19, ((uint64_t)8ULL));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l0;
        uint8_t *t23 = (((uint8_t *)t22) + 0);
        gm_b16_8 t24 = gm_f_empty_slice_p5lo8ed();
        GM_ST(gm_b16_8, t23, t24);
    }
    {
        void * t25 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t25) + 16), ((uint64_t)0ULL));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_ectdopm_2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_ectdopm_2 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 16));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, ((((uint8_t *)t7) + 24) + 8));
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 16));
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, ((((uint8_t *)t10) + 24) + 0));
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            void * t14 = t13;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 16));
            uint64_t r17; bool o18 = __builtin_mul_overflow(((uint64_t)48ULL), t16, &r17);
            if (o18) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 143, 64);
            uint64_t t19 = r17;
            void * t20 = t9;
            gm_b0_1 t21;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t20))(t11, t14, t19, ((uint64_t)8ULL));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l0;
        uint8_t *t23 = (((uint8_t *)t22) + 0);
        gm_b16_8 t24 = gm_f_empty_slice_p5lo8ed_3();
        GM_ST(gm_b16_8, t23, t24);
    }
    {
        void * t25 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t25) + 16), ((uint64_t)0ULL));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_u4v62un(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_u4v62un F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 16));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, ((((uint8_t *)t7) + 24) + 8));
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 16));
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, ((((uint8_t *)t10) + 24) + 0));
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            void * t14 = t13;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 16));
            uint64_t r17; bool o18 = __builtin_mul_overflow(((uint64_t)32ULL), t16, &r17);
            if (o18) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 143, 64);
            uint64_t t19 = r17;
            void * t20 = t9;
            gm_b0_1 t21;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t20))(t11, t14, t19, ((uint64_t)8ULL));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l0;
        uint8_t *t23 = (((uint8_t *)t22) + 0);
        gm_b16_8 t24 = gm_f_empty_slice_m3oe7t3();
        GM_ST(gm_b16_8, t23, t24);
    }
    {
        void * t25 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t25) + 16), ((uint64_t)0ULL));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_e1j40m3(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_e1j40m3 F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 361u);
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
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 77, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_967kt0a_2(t14, t15, t16);
            gm_tftb1odc739t5 t18 = t17;
            if (((((uint8_t *)&t18))[0] != 0)) {
                gm_t9qkh0rsooc8d t19 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t18) + 1));
                gm_tftb1odc739t5 t20;
                ((uint8_t *)&t20)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 1), t19);
                F.ret = t20;
                goto gm_fret;
            }
            gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
            void * t25 = ((uint8_t *)t24);
            uint64_t t26 = gm_l2;
            uint64_t t27 = (uintptr_t)t26;
            uint64_t t28 = gm_l2;
            uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r30; bool o31 = __builtin_add_overflow(t28, t29, &r30);
            if (o31) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 81, 36);
            uint64_t t32 = r30;
            uint64_t t33 = (uintptr_t)t32;
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t25 + t27 * 32)));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(t33 - t27));
            gm_b16_8 t35 = t34;
            gm_b16_8 t36 = gm_l1;
            gm_b16_8 t37 = t36;
            if (GM_LD(uintptr_t, ((uint8_t *)&t35) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t35)), GM_LD(void *, ((uint8_t *)&t37)), GM_LD(uintptr_t, ((uint8_t *)&t35) + 8) * 32);
        }
ix4:;
    }
    {
        void * t38 = gm_l0;
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
        void * t41 = ((uint8_t *)t40);
        uint64_t t42 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t43 = gm_l2;
        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r45; bool o46 = __builtin_add_overflow(t43, t44, &r45);
        if (o46) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 83, 30);
        uint64_t t47 = r45;
        uint64_t t48 = (uintptr_t)t47;
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t41 + t42 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(t48 - t42));
        GM_ST(gm_b16_8, (((uint8_t *)t38) + 0), t49);
    }
    {
        gm_b0_1 t50;
        gm_tftb1odc739t5 t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
        ((uint8_t *)&t51)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t51 + 1), t50);
        gm_tftb1odc739t5 t52 = t51;
        uint8_t t53 = ((((uint8_t *)&t52))[0] != 0);
        F.ret = t51;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_ehh2367(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_ehh2367 F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 361u);
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
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 77, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_78ppoki(t14, t15, t16);
            gm_tftb1odc739t5 t18 = t17;
            if (((((uint8_t *)&t18))[0] != 0)) {
                gm_t9qkh0rsooc8d t19 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t18) + 1));
                gm_tftb1odc739t5 t20;
                ((uint8_t *)&t20)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 1), t19);
                F.ret = t20;
                goto gm_fret;
            }
            gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
            void * t25 = ((uint8_t *)t24);
            uint64_t t26 = gm_l2;
            uint64_t t27 = (uintptr_t)t26;
            uint64_t t28 = gm_l2;
            uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r30; bool o31 = __builtin_add_overflow(t28, t29, &r30);
            if (o31) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 81, 36);
            uint64_t t32 = r30;
            uint64_t t33 = (uintptr_t)t32;
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t25 + t27 * 224)));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(t33 - t27));
            gm_b16_8 t35 = t34;
            gm_b16_8 t36 = gm_l1;
            gm_b16_8 t37 = t36;
            if (GM_LD(uintptr_t, ((uint8_t *)&t35) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t35)), GM_LD(void *, ((uint8_t *)&t37)), GM_LD(uintptr_t, ((uint8_t *)&t35) + 8) * 224);
        }
ix4:;
    }
    {
        void * t38 = gm_l0;
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
        void * t41 = ((uint8_t *)t40);
        uint64_t t42 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t43 = gm_l2;
        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r45; bool o46 = __builtin_add_overflow(t43, t44, &r45);
        if (o46) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 83, 30);
        uint64_t t47 = r45;
        uint64_t t48 = (uintptr_t)t47;
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t41 + t42 * 224)));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(t48 - t42));
        GM_ST(gm_b16_8, (((uint8_t *)t38) + 0), t49);
    }
    {
        gm_b0_1 t50;
        gm_tftb1odc739t5 t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
        ((uint8_t *)&t51)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t51 + 1), t50);
        gm_tftb1odc739t5 t52 = t51;
        uint8_t t53 = ((((uint8_t *)&t52))[0] != 0);
        F.ret = t51;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_eht4gub(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_eht4gub F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 361u);
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
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 77, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_1dn6qmo(t14, t15, t16);
            gm_tftb1odc739t5 t18 = t17;
            if (((((uint8_t *)&t18))[0] != 0)) {
                gm_t9qkh0rsooc8d t19 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t18) + 1));
                gm_tftb1odc739t5 t20;
                ((uint8_t *)&t20)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 1), t19);
                F.ret = t20;
                goto gm_fret;
            }
            gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
            void * t25 = ((uint8_t *)t24);
            uint64_t t26 = gm_l2;
            uint64_t t27 = (uintptr_t)t26;
            uint64_t t28 = gm_l2;
            uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r30; bool o31 = __builtin_add_overflow(t28, t29, &r30);
            if (o31) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 81, 36);
            uint64_t t32 = r30;
            uint64_t t33 = (uintptr_t)t32;
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t25 + t27 * 136)));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(t33 - t27));
            gm_b16_8 t35 = t34;
            gm_b16_8 t36 = gm_l1;
            gm_b16_8 t37 = t36;
            if (GM_LD(uintptr_t, ((uint8_t *)&t35) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t35)), GM_LD(void *, ((uint8_t *)&t37)), GM_LD(uintptr_t, ((uint8_t *)&t35) + 8) * 136);
        }
ix4:;
    }
    {
        void * t38 = gm_l0;
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
        void * t41 = ((uint8_t *)t40);
        uint64_t t42 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t43 = gm_l2;
        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r45; bool o46 = __builtin_add_overflow(t43, t44, &r45);
        if (o46) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 83, 30);
        uint64_t t47 = r45;
        uint64_t t48 = (uintptr_t)t47;
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t41 + t42 * 136)));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(t48 - t42));
        GM_ST(gm_b16_8, (((uint8_t *)t38) + 0), t49);
    }
    {
        gm_b0_1 t50;
        gm_tftb1odc739t5 t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
        ((uint8_t *)&t51)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t51 + 1), t50);
        gm_tftb1odc739t5 t52 = t51;
        uint8_t t53 = ((((uint8_t *)&t52))[0] != 0);
        F.ret = t51;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05foihn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_097u1ss(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1qdv1d5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3de08l2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3if10s4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_47323no(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6ms6csh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9sjif99(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_co27i32(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_d9c49u0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dkb05ql(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f60m3fk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fsudkd6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jotf1n0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l7nrntj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l80efih(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ll0fd0a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mj05e1r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n1ut9t3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n85ptlm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_npkjc6s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o5t93u5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qffm308(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sar6mkr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sd7157p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ucp1n4i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_insert_u31m55g(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insert_u31m55g F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* x */
    uint64_t gm_l3; /* n */
    gm_b16_8 gm_l4; /* grown */
    GM_STACK_CHECK(&F, 481u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 > t5);
        if (!t6) goto ie2;
        {
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s8lpasdulkmbv));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(27));
            gm_b16_8 t9 = t8;
            gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t9)), GM_LD(uintptr_t, ((uint8_t *)&t9) + 8), gm_srcrgc9bkquf6nr, 90, 26);
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        gm_tftb1odc739t5 t11 = gm_f_reserve_m6out5p(t10, ((uint64_t)1ULL));
        gm_tftb1odc739t5 t12 = t11;
        if (((((uint8_t *)&t12))[0] != 0)) {
            gm_t9qkh0rsooc8d t13 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t12) + 1));
            gm_tftb1odc739t5 t14;
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 1), t13);
            F.ret = t14;
            goto gm_fret;
        }
        gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
    }
    {
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uintptr_t, (((uint8_t *)t16) + 0) + 8);
        gm_l3 = t17;
    }
    {
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
        void * t20 = ((uint8_t *)t19);
        uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t22 = gm_l3;
        uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
        if (o24) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 93, 29);
        uint64_t t25 = r23;
        uint64_t t26 = (uintptr_t)t25;
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t20 + t21 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - t21));
        gm_l4 = t27;
    }
    {
        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
        uint64_t t30 = gm_l1;
        uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
        if (o32) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 94, 23);
        uint64_t t33 = r31;
        uint64_t t34 = (uintptr_t)t33;
        uint64_t t35 = gm_l3;
        uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
        if (o37) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 94, 30);
        uint64_t t38 = r36;
        uint64_t t39 = (uintptr_t)t38;
        if (t34 > t39 || t39 > (uintptr_t)t28) gm_trap_slice(t34, t39, t28, gm_srcrgc9bkquf6nr, 94, 17);
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(((uint8_t *)t29 + t34 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(t39 - t34));
        gm_b16_8 t41 = t40;
        uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
        uint64_t t44 = gm_l1;
        uint64_t t45 = (uintptr_t)t44;
        uint64_t t46 = gm_l3;
        uint64_t t47 = (uintptr_t)t46;
        if (t45 > t47 || t47 > (uintptr_t)t42) gm_trap_slice(t45, t47, t42, gm_srcrgc9bkquf6nr, 94, 38);
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t43 + t45 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t45));
        gm_b16_8 t49 = t48;
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        gm_b16_8 t51 = t50;
        if (GM_LD(uintptr_t, ((uint8_t *)&t41) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t41)), GM_LD(void *, ((uint8_t *)&t51)), GM_LD(uintptr_t, ((uint8_t *)&t41) + 8) * 8);
    }
    {
        uint64_t t52 = gm_l1;
        uint64_t t53 = (uintptr_t)t52;
        uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t53 >= (uintptr_t)(t54)) gm_trap_index(t53, t54, gm_srcrgc9bkquf6nr, 95, 5);
        uint64_t t55 = gm_l2;
        GM_ST(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t53 * 8), t55);
    }
    {
        void * t56 = gm_l0;
        gm_b16_8 t57 = gm_l4;
        GM_ST(gm_b16_8, (((uint8_t *)t56) + 0), t57);
    }
    {
        gm_b0_1 t58;
        gm_tftb1odc739t5 t59;
        GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
        ((uint8_t *)&t59)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t59 + 1), t58);
        gm_tftb1odc739t5 t60 = t59;
        uint8_t t61 = ((((uint8_t *)&t60))[0] != 0);
        F.ret = t59;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_into_slice_e14ea2s(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_into_slice_e14ea2s F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* size */
    void * gm_l3; /* old */
    void * gm_l4; /* raw */
    void * gm_l5; /* p */
    gm_b16_8 gm_l6; /* s */
    GM_STACK_CHECK(&F, 1042u);
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
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie4;
        {
            gm_b16_8 t9 = gm_f_empty_slice_dheabhi_3();
            gm_tabb710d9mq2e t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            ((uint8_t *)&t10)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 8), t9);
            gm_tabb710d9mq2e t11 = t10;
            uint8_t t12 = ((((uint8_t *)&t11))[0] != 0);
            F.ret = t10;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t14 = gm_l1;
        uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
        if (!t15) goto ie13;
        {
            void * t17 = gm_l0;
            gm_f_deinit_fac02bd(t17);
        }
        {
            gm_b16_8 t18 = gm_f_empty_slice_dheabhi_3();
            gm_tabb710d9mq2e t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            ((uint8_t *)&t19)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 8), t18);
            gm_tabb710d9mq2e t20 = t19;
            uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
            F.ret = t19;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_l2 = ((uint64_t)24ULL);
    }
    {
        uint64_t t23 = gm_l1;
        void * t24 = gm_l0;
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 16));
        uint8_t t26 = (t23 < t25);
        if (!t26) goto ie22;
        {
            void * t28 = gm_l0;
            void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
            void * t30 = t29;
            gm_l3 = t30;
        }
        {
            void * t32 = gm_l0;
            void * t33 = GM_LD(void *, ((((uint8_t *)t32) + 24) + 8));
            void * t34 = GM_LD(void *, (((uint8_t *)t33) + 8));
            void * t35 = gm_l0;
            void * t36 = GM_LD(void *, ((((uint8_t *)t35) + 24) + 0));
            void * t37 = gm_l3;
            uint64_t t38 = gm_l2;
            void * t39 = gm_l0;
            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 16));
            uint64_t r41; bool o42 = __builtin_mul_overflow(t38, t40, &r41);
            if (o42) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 161, 53);
            uint64_t t43 = r41;
            uint64_t t44 = gm_l2;
            uint64_t t45 = gm_l1;
            uint64_t r46; bool o47 = __builtin_mul_overflow(t44, t45, &r46);
            if (o47) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 161, 67);
            uint64_t t48 = r46;
            void * t49 = t34;
            uint8_t t50;
            t50 = ((uint8_t (*)(void *, void *, uint64_t, uint64_t, uint64_t))GM_PTR_UNTAG(t49))(t36, t37, t43, t48, ((uint64_t)8ULL));
            uint8_t t51 = (uint8_t)!t50;
            if (!t51) goto ie31;
            {
                void * t53 = gm_l0;
                void * t54 = GM_LD(void *, ((((uint8_t *)t53) + 24) + 8));
                void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
                void * t56 = gm_l0;
                void * t57 = GM_LD(void *, ((((uint8_t *)t56) + 24) + 0));
                uint64_t t58 = gm_l2;
                uint64_t t59 = gm_l1;
                uint64_t r60; bool o61 = __builtin_mul_overflow(t58, t59, &r60);
                if (o61) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 162, 51);
                uint64_t t62 = r60;
                void * t63 = t55;
                void * t64;
                t64 = ((void * (*)(void *, uint64_t, uint64_t))GM_PTR_UNTAG(t63))(t57, t62, ((uint64_t)8ULL));
                void * t65 = t64;
                void * t66;
                if ((GM_LD(void *, ((uint8_t *)&t65)) != 0)) {
                    void * t67 = GM_LD(void *, (((uint8_t *)&t65) + 0));
                    t66 = t67;
                } else {
                    gm_t9qkh0rsooc8d t68;
                    GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)0ULL));
                    gm_tabb710d9mq2e t69;
                    GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t69 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t69 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t69 + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&t69 + 16, 0);
                    ((uint8_t *)&t69)[0] = 1;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t69 + 8), t68);
                    gm_tabb710d9mq2e t70 = t69;
                    uint8_t t71 = ((((uint8_t *)&t70))[0] != 0);
                    F.ret = t69;
                    goto gm_fret;
                }
                gm_l4 = t66;
            }
            {
                void * t72 = gm_l4;
                if ((uintptr_t)t72 % 8 != 0) gm_trap_misaligned((uintptr_t)t72, 8, gm_srcrgc9bkquf6nr, 163, 18);
                void * t73 = t72;
                gm_l5 = t73;
            }
            {
                void * t74 = gm_l5;
                void * t75 = ((uint8_t *)t74);
                uint64_t t76 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t77 = gm_l1;
                uint64_t t78 = (uintptr_t)t77;
                gm_b16_8 t79;
                GM_ST(void *, (uint8_t *)&t79, (void *)(((uint8_t *)t75 + t76 * 24)));
                GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(t78 - t76));
                gm_b16_8 t80 = t79;
                void * t81 = gm_l0;
                gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)t81) + 0));
                gm_b16_8 t83 = t82;
                gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                gm_b16_8 t85 = t84;
                if (GM_LD(uintptr_t, ((uint8_t *)&t80) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t80)), GM_LD(void *, ((uint8_t *)&t85)), GM_LD(uintptr_t, ((uint8_t *)&t80) + 8) * 24);
            }
            {
                void * t86 = gm_l0;
                void * t87 = GM_LD(void *, ((((uint8_t *)t86) + 24) + 8));
                void * t88 = GM_LD(void *, (((uint8_t *)t87) + 16));
                void * t89 = gm_l0;
                void * t90 = GM_LD(void *, ((((uint8_t *)t89) + 24) + 0));
                void * t91 = gm_l3;
                uint64_t t92 = gm_l2;
                void * t93 = gm_l0;
                uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)t93) + 16));
                uint64_t r95; bool o96 = __builtin_mul_overflow(t92, t94, &r95);
                if (o96) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 165, 48);
                uint64_t t97 = r95;
                void * t98 = t88;
                gm_b0_1 t99;
                ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t98))(t90, t91, t97, ((uint64_t)8ULL));
            }
            {
                void * t100 = gm_l0;
                void * t101 = gm_l5;
                void * t102 = ((uint8_t *)t101);
                uint64_t t103 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t104 = gm_l1;
                uint64_t t105 = (uintptr_t)t104;
                gm_b16_8 t106;
                GM_ST(void *, (uint8_t *)&t106, (void *)(((uint8_t *)t102 + t103 * 24)));
                GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(t105 - t103));
                GM_ST(gm_b16_8, (((uint8_t *)t100) + 0), t106);
            }
            goto ix31;
ie31:;
ix31:;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t107 = gm_l0;
        void * t108 = GM_LD(void *, (((uint8_t *)t107) + 0));
        void * t109 = ((uint8_t *)t108);
        uint64_t t110 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t111 = gm_l1;
        uint64_t t112 = (uintptr_t)t111;
        gm_b16_8 t113;
        GM_ST(void *, (uint8_t *)&t113, (void *)(((uint8_t *)t109 + t110 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(t112 - t110));
        gm_l6 = t113;
    }
    {
        void * t114 = gm_l0;
        uint8_t *t115 = (((uint8_t *)t114) + 0);
        gm_b16_8 t116 = gm_f_empty_slice_dheabhi_3();
        GM_ST(gm_b16_8, t115, t116);
    }
    {
        void * t117 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t117) + 16), ((uint64_t)0ULL));
    }
    {
        gm_b16_8 t118 = gm_l6;
        gm_tabb710d9mq2e t119;
        GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
        ((uint8_t *)&t119)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t119 + 8), t118);
        gm_tabb710d9mq2e t120 = t119;
        uint8_t t121 = ((((uint8_t *)&t120))[0] != 0);
        F.ret = t119;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tabb710d9mq2e gm_f_into_slice_e14ea2s_c(void * a0) { gm_tabb710d9mq2e gm_r; gm_f_into_slice_e14ea2s(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_list_e6ie1fq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_e6ie1fq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_eb5ua5r(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_eh2bcst(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_u1fokev(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_uaa66su(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_uahmact(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_ubba23m(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_ubba23m_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_uc1atd0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_ue2t2bm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_umenr17(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_urufktd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_uun92du(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_pop_u3dhp9e(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_toql92ug1137o gm_f_pop_u3dhp9e_c(void * a0) { gm_toql92ug1137o gm_r; gm_f_pop_u3dhp9e(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_eb7kbhp F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 345u);
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
            gm_b16_8 t11 = gm_f_empty_slice_lmbdkhb();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_4bta8ne(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 80)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tq5j0547p8452 t32 = F.l1;
        GM_ST(gm_tq5j0547p8452, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 80), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_eb7kbhp_2 F;
    void * gm_l0; /* l */
    gm_tlr9dp305htm7 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t11 = gm_f_empty_slice_lmbdkhb_2();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_4bta8ne_2(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tlr9dp305htm7 t32 = gm_l1;
        GM_ST(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 24), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp_2_c(void * a0, gm_tlr9dp305htm7 a1) { return gm_f_push_eb7kbhp_2(a0, &a1); }
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp_c(void * a0, gm_tq5j0547p8452 a1) { return gm_f_push_eb7kbhp(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ehvjfcg(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ehvjfcg F;
    void * gm_l0; /* l */
    gm_t3ujhu6qph7de gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t11 = gm_f_empty_slice_p8ki924();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_t4b6ipk(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t3ujhu6qph7de t32 = GM_LD(gm_t3ujhu6qph7de, ((uint8_t *)a1));
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 24), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ehvjfcg_c(void * a0, gm_t3ujhu6qph7de a1) { return gm_f_push_ehvjfcg(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ej78v89(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ej78v89 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 305u);
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
            gm_b16_8 t11 = gm_f_empty_slice_icu3qs9();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_pcenrov(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tk9n3vlila9qg t32 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)a1));
        GM_ST(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 40), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ej78v89_c(void * a0, gm_tk9n3vlila9qg a1) { return gm_f_push_ej78v89(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_em9e1kh(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_em9e1kh F;
    void * gm_l0; /* l */
    gm_t16on0i8enr1i gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 329u);
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
            gm_b16_8 t11 = gm_f_empty_slice_n5n7pq1();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_r8ccoha(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t16on0i8enr1i t32 = GM_LD(gm_t16on0i8enr1i, ((uint8_t *)a1));
        GM_ST(gm_t16on0i8enr1i, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 32), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_em9e1kh_c(void * a0, gm_t16on0i8enr1i a1) { return gm_f_push_em9e1kh(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_esf6o94(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_esf6o94 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 305u);
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
            gm_b16_8 t11 = gm_f_empty_slice_hmpovae();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_vlhhu7d(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t2k9dbsv5ff58 t32 = GM_LD(gm_t2k9dbsv5ff58, ((uint8_t *)a1));
        GM_ST(gm_t2k9dbsv5ff58, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 40), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_esf6o94_c(void * a0, gm_t2k9dbsv5ff58 a1) { return gm_f_push_esf6o94(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u0rp7b1(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u0rp7b1 F;
    void * gm_l0; /* l */
    gm_t2lqu8jsj511d gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 297u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
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
            gm_b16_8 t11 = gm_f_empty_slice_jjs3scl();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_r9l0iqd(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t2lqu8jsj511d t32 = gm_l1;
        GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 16), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u2jka7o(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u2jka7o F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 281u);
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
            gm_b16_8 t11 = gm_f_empty_slice_rtces00();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_94j8fj1(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        void * t32 = gm_l1;
        GM_ST(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 8), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u2jka7o_2(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u2jka7o_2 F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 281u);
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
            gm_b16_8 t11 = gm_f_empty_slice_rtces00_2();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_94j8fj1_2(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        void * t32 = gm_l1;
        GM_ST(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 8), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u46i39f(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u46i39f F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 305u);
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
            gm_b16_8 t11 = gm_f_empty_slice_i0ticdn();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_q7kpg21(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_trqfvmjtt4sij t32 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)a1));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 40), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u46i39f_c(void * a0, gm_trqfvmjtt4sij a1) { return gm_f_push_u46i39f(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u4kbe6d(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u4kbe6d F;
    void * gm_l0; /* l */
    gm_t5g5vgung6h6s gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 329u);
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
            gm_b16_8 t11 = gm_f_empty_slice_f1ngfj0();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_f2uhbto(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_t5g5vgung6h6s t32 = GM_LD(gm_t5g5vgung6h6s, ((uint8_t *)a1));
        GM_ST(gm_t5g5vgung6h6s, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 32), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u4kbe6d_c(void * a0, gm_t5g5vgung6h6s a1) { return gm_f_push_u4kbe6d(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u9lban8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u9lban8 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 433u);
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
            gm_b16_8 t11 = gm_f_empty_slice_30da7i6();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_3iuca1r(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 168)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tp8h583g7s5hd t32 = GM_LD(gm_tp8h583g7s5hd, ((uint8_t *)a1));
        GM_ST(gm_tp8h583g7s5hd, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 168), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u9lban8_c(void * a0, gm_tp8h583g7s5hd a1) { return gm_f_push_u9lban8(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ucgmlo2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 281u);
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
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_k4j2gj3(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        uint64_t t32 = gm_l1;
        GM_ST(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 8), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_uehemid(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_uehemid F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 281u);
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
            gm_b16_8 t11 = gm_f_empty_slice_73gbl6u();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_nd6prsl(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        void * t32 = gm_l1;
        GM_ST(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 8), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_usak761(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_usak761 F;
    void * gm_l0; /* l */
    gm_tgk5j0pob2pqa gm_l1; /* x */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* grown */
    GM_STACK_CHECK(&F, 313u);
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
            gm_b16_8 t11 = gm_f_empty_slice_vefkkhp();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_tftb1odc739t5 t14 = gm_f_regrow_our9l6p(t10, ((uint64_t)1ULL), t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t15) + 1));
                gm_tftb1odc739t5 t17;
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = ((uint8_t *)t20);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l2;
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 68, 29);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        gm_l3 = t28;
    }
    {
        uint64_t t29 = gm_l2;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcrgc9bkquf6nr, 69, 5);
        gm_tgk5j0pob2pqa t32 = GM_LD(gm_tgk5j0pob2pqa, ((uint8_t *)a1));
        GM_ST(gm_tgk5j0pob2pqa, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 24), t32);
    }
    {
        void * t33 = gm_l0;
        gm_b16_8 t34 = gm_l3;
        GM_ST(gm_b16_8, (((uint8_t *)t33) + 0), t34);
    }
    {
        gm_b0_1 t35;
        gm_tftb1odc739t5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_tftb1odc739t5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_usak761_c(void * a0, gm_csqm5vmbpo6ge8 a1) { return gm_f_push_usak761(a0, &a1); }
extern __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_e4d8f0p(void * a0, uint64_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_e5frfkp(void * a0, uint64_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_edp3chb(void * a0, uint64_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_efufsip(void * a0, uint64_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_egu240o(void * a0, uint64_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emhp9ia(void * a0, uint64_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_eml2a4s(void * a0, uint64_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_u824ov8(void * a0, uint64_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_u824ov8_2(void * a0, uint64_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ub69oip(void * a0, uint64_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_urfcki5(void * a0, uint64_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_urfcki5_2(void * a0, uint64_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_usnqfd3(void * a0, uint64_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) void gm_f_truncate_eoppkeo(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_truncate_ua7l21h(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_truncate_ucmmehv(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e15chue(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_e15chue F;
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tr4ummvbupakb t3 = GM_LD(gm_tr4ummvbupakb, ((uint8_t *)a1));
            F.l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_l80efih, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t6 = t5;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 16));
            uint64_t t8 = t7;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 24));
            uint64_t t10 = t9;
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t12 = t11;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 48));
            uint64_t t14 = t13;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 56));
            uint64_t t16 = t15;
            gm_closure t17 = t4;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sda1bf43v6985, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_skfdo9qggtrbp, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tyb2nno7n67r, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sqrh6i8djv6b4, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tyb2nno7n67r, ((uint8_t *)&t10), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_snvodad5t43r7, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty0iqiubfm7e, ((uint8_t *)&t12), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_skfdo9qggtrbp, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tyb2nno7n67r, ((uint8_t *)&t14), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sqrh6i8djv6b4, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tyb2nno7n67r, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_snr5o5tak6405, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t23;
            ((uint8_t *)&t23)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 1), t22);
            gm_tftb1odc739t5 t24 = t23;
            uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
            F.ret = t23;
            goto gm_fret;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        gm_b0_1 t26;
        gm_tftb1odc739t5 t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
        ((uint8_t *)&t27)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t27 + 1), t26);
        gm_tftb1odc739t5 t28 = t27;
        uint8_t t29 = ((((uint8_t *)&t28))[0] != 0);
        F.ret = t27;
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e15chue_c(void * a0, gm_tr4ummvbupakb a1) { return gm_f_write_e15chue(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e9824eu(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_e9824eu F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_3de08l2, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ed06dp1(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ed06dp1 F;
    gm_tf1bjg81u0glv gm_l1; /* args */
    gm_tf1bjg81u0glv gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_qffm308, (void *)&F.h, cx});
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_s0jrrhhm4skoj, 14)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_tyb2nno7n67r, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_sp1nce97af2b9, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_sgq8hfs47hogb, 8)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ed06dp1_c(void * a0, gm_tf1bjg81u0glv a1) { return gm_f_write_ed06dp1(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ef6g7s0(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ef6g7s0 F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_6ms6csh, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_shmpls8n0g8h9, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_epd64m2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_epd64m2 F;
    gm_t6j6smj7qoctu gm_l1; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_dkb05ql, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_s0fim1krogkdi, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_svgje5djlutuu, 24)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_so7oqi7vob9ku, 16)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_epd64m2_c(void * a0, gm_t6j6smj7qoctu a1) { return gm_f_write_epd64m2(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_erog63d(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_erog63d F;
    gm_tcc1rq4n1avb2 gm_l1; /* args */
    gm_tcc1rq4n1avb2 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tcc1rq4n1avb2 t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_9sjif99, (void *)&F.h, cx});
            uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t6 = t5;
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            uint32_t t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_s5vogfeoqmvr7, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty467d5cm1d5, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_sc8o4oaph9gbr, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty467d5cm1d5, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u0r3vc4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u0r3vc4 F;
    gm_t5hdnnftkup6m gm_l1; /* args */
    gm_t5hdnnftkup6m gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t5hdnnftkup6m t3 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_l7nrntj, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 16));
            uint32_t t8 = t7;
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 20));
            uint32_t t10 = t9;
            gm_closure t11 = t4;
            gm_node *ccx12 = t11.cx;
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_s39mljh669qgl, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty467d5cm1d5, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_s39mljh669qgl, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty467d5cm1d5, ((uint8_t *)&t10), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_slgmqgkioa4u6, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t14 = F.l2;
        if (!t14) goto ie13;
        {
            gm_t9qkh0rsooc8d t16;
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t17;
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
            gm_tftb1odc739t5 t18 = t17;
            uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
            F.ret = t17;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_b0_1 t20;
        gm_tftb1odc739t5 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        ((uint8_t *)&t21)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t21 + 1), t20);
        gm_tftb1odc739t5 t22 = t21;
        uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
        F.ret = t21;
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u0r3vc4_c(void * a0, gm_t5hdnnftkup6m a1) { return gm_f_write_u0r3vc4(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u1no4ia(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u1no4ia F;
    gm_t3877pk7lv748 gm_l1; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_ll0fd0a, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_snaenr23tbgmb, 22)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_s3347hvco05di, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_tyb2nno7n67r, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_s4l2s2smm9apo, 20)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u1no4ia_c(void * a0, gm_t3877pk7lv748 a1) { return gm_f_write_u1no4ia(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u3br5ci(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u3br5ci F;
    GM_STACK_CHECK(&F, 304u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a1));
            F.l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_d9c49u0, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t8 = t7;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t10 = t9;
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 64));
            gm_b16_8 t14 = t13;
            gm_closure t15 = t4;
            gm_node *ccx16 = t15.cx;
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_sasea0sobfkrs, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_ty0iqiubfm7e, ((uint8_t *)&t10), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_tybihgc69jv3, ((uint8_t *)&t12), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_sg5e6vqb8nie3, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t15.code, t15.env, ccx16, &gm_ty0iqiubfm7e, ((uint8_t *)&t14), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t15.code, t15.env, ccx16, gm_sdhelph0q09tv, 4)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t18 = F.l2;
        if (!t18) goto ie17;
        {
            gm_t9qkh0rsooc8d t20;
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t21;
            ((uint8_t *)&t21)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21 + 1), t20);
            gm_tftb1odc739t5 t22 = t21;
            uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
            F.ret = t21;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        gm_b0_1 t24;
        gm_tftb1odc739t5 t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
        ((uint8_t *)&t25)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t25 + 1), t24);
        gm_tftb1odc739t5 t26 = t25;
        uint8_t t27 = ((((uint8_t *)&t26))[0] != 0);
        F.ret = t25;
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u3br5ci_c(void * a0, gm_tud7oovfvnf43 a1) { return gm_f_write_u3br5ci(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u4hj8pn(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u4hj8pn F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_ucp1n4i, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_ssfg877iegnma, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_s97nfli622uqg, 4)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u50pgu2(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u50pgu2 F;
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_f60m3fk, (void *)&F.h, cx});
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sk754mj3p1el6, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_tyb2nno7n67r, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_s2053hhvf16fm, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u5ur1qg(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u5ur1qg F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_mj05e1r, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_stb42390ups9k, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u8qima9(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u8qima9 F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_05foihn, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sdmle2qsm85vh, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u8u1amh(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u8u1amh F;
    GM_STACK_CHECK(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a1));
            F.l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_n85ptlm, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t8 = t7;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t10 = t9;
            gm_closure t11 = t4;
            gm_node *ccx12 = t11.cx;
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_som0q53jqvjh3, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_sbnuvhnotkfkr, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_s4dun9lv4h66e, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty0iqiubfm7e, ((uint8_t *)&t10), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_snr5o5tak6405, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t14 = F.l2;
        if (!t14) goto ie13;
        {
            gm_t9qkh0rsooc8d t16;
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t17;
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
            gm_tftb1odc739t5 t18 = t17;
            uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
            F.ret = t17;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_b0_1 t20;
        gm_tftb1odc739t5 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        ((uint8_t *)&t21)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t21 + 1), t20);
        gm_tftb1odc739t5 t22 = t21;
        uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
        F.ret = t21;
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u8u1amh_c(void * a0, gm_tfeab279c16rr a1) { return gm_f_write_u8u1amh(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u9o0juu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u9o0juu F;
    gm_t6j6smj7qoctu gm_l1; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_sar6mkr, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_shc0s7vbfquc0, 19)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_skpgi6v1lj8m9, 18)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_sdhelph0q09tv, 4)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u9o0juu_c(void * a0, gm_t6j6smj7qoctu a1) { return gm_f_write_u9o0juu(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ud12ae8(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ud12ae8 F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_co27i32, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_s9md3pg6vlt5h, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sgtmc08t3k49t, 41)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ue93873(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ue93873 F;
    gm_t6j6smj7qoctu gm_l1; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_fsudkd6, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_spj9rva8kb5ar, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_tybihgc69jv3, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_s77fi68iu5q6n, 4)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ue93873_c(void * a0, gm_t6j6smj7qoctu a1) { return gm_f_write_ue93873(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uebhmoa(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_uebhmoa F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_1qdv1d5, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_svsf0pu3t8u6m, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uf08lpv(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_uf08lpv F;
    gm_t6j6smj7qoctu gm_l1; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_n1ut9t3, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_sabu5uehm65n2, 21)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_s9sev457i3j7n, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_tybihgc69jv3, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_sg398rgmck67b, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uf08lpv_c(void * a0, gm_t6j6smj7qoctu a1) { return gm_f_write_uf08lpv(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ufh35j8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ufh35j8 F;
    gm_t6j6smj7qoctu gm_l1; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_jotf1n0, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_s8a7jvbqoerpn, 20)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_se47039ecu2b8, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_tybihgc69jv3, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_sikpqtja8pqpv, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ufh35j8_c(void * a0, gm_t6j6smj7qoctu a1) { return gm_f_write_ufh35j8(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ug67q96(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ug67q96 F;
    gm_tnen6863q2sjs gm_l1; /* args */
    gm_tnen6863q2sjs gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnen6863q2sjs t3 = GM_LD(gm_tnen6863q2sjs, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_o5t93u5, (void *)&F.h, cx});
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t6 = t5;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t8 = t7;
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 16));
            uint32_t t10 = t9;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 24));
            uint64_t t12 = t11;
            gm_closure t13 = t4;
            gm_node *ccx14 = t13.cx;
            if (gm_pf0aqhubr4up(t13.code, t13.env, ccx14, gm_snvldulppvqal, 22)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t13.code, t13.env, ccx14, &gm_ty6groq37ram, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t13.code, t13.env, ccx14, gm_s52fbscfsv3pa, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t13.code, t13.env, ccx14, &gm_ty6groq37ram, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t13.code, t13.env, ccx14, gm_ss8lvrvkhracl, 20)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t13.code, t13.env, ccx14, &gm_ty467d5cm1d5, ((uint8_t *)&t10), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t13.code, t13.env, ccx14, gm_sjbjq95a18dio, 43)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t13.code, t13.env, ccx14, &gm_ty6groq37ram, ((uint8_t *)&t12), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t13.code, t13.env, ccx14, gm_s6t4r831mukdr, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t16 = F.l2;
        if (!t16) goto ie15;
        {
            gm_t9qkh0rsooc8d t18;
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t19;
            ((uint8_t *)&t19)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
            gm_tftb1odc739t5 t20 = t19;
            uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
            F.ret = t19;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        gm_b0_1 t22;
        gm_tftb1odc739t5 t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        ((uint8_t *)&t23)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t23 + 1), t22);
        gm_tftb1odc739t5 t24 = t23;
        uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ug67q96_c(void * a0, gm_tnen6863q2sjs a1) { return gm_f_write_ug67q96(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_upbce2g(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_upbce2g F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_sd7157p, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sjs62u07kjioq, 14)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_spbhhmdbp335u, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_upr5k5n(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_upr5k5n F;
    gm_tnpifv20irnph gm_l1; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_3if10s4, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_s16mf4cvajh59, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_tybihgc69jv3, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sgosg5fctcl72, 16)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uqdv1oa(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_uqdv1oa F;
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_47323no, (void *)&F.h, cx});
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t6 = t5;
            gm_closure t7 = t4;
            gm_node *ccx8 = t7.cx;
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_shakeh6dfb84i, 16)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t7.code, t7.env, ccx8, &gm_ty6groq37ram, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t7.code, t7.env, ccx8, gm_sf4qi8ut6u3p1, 4)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t10 = F.l2;
        if (!t10) goto ie9;
        {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t13;
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 1), t12);
            gm_tftb1odc739t5 t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b0_1 t16;
        gm_tftb1odc739t5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
        gm_tftb1odc739t5 t18 = t17;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_usci3o4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_usci3o4 F;
    gm_t6j6smj7qoctu gm_l1; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
            gm_l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_097u1ss, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t6 = t5;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t8 = t7;
            gm_closure t9 = t4;
            gm_node *ccx10 = t9.cx;
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_som0q53jqvjh3, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_scedkbh0j6d78, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t9.code, t9.env, ccx10, &gm_ty0iqiubfm7e, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t9.code, t9.env, ccx10, gm_s2kl2d59c04nf, 18)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t12 = F.l2;
        if (!t12) goto ie11;
        {
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_b0_1 t18;
        gm_tftb1odc739t5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
        gm_tftb1odc739t5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_usci3o4_c(void * a0, gm_t6j6smj7qoctu a1) { return gm_f_write_usci3o4(a0, &a1); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uuia5cm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_uuia5cm F;
    GM_STACK_CHECK(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tq3m5qgvkpje8 t3 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a1));
            F.l3 = t3;
        }
        {
            gm_closure t4 = ((gm_closure){(void *)gm_f_fn_npkjc6s, (void *)&F.h, cx});
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t6 = t5;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 16));
            uint64_t t8 = t7;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
            gm_b16_8 t10 = t9;
            gm_closure t11 = t4;
            gm_node *ccx12 = t11.cx;
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_s48iukd74s6c6, 21)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty0iqiubfm7e, ((uint8_t *)&t6), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_sk754mj3p1el6, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty6groq37ram, ((uint8_t *)&t8), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_s1d673tn03i3r, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t11.code, t11.env, ccx12, &gm_ty0iqiubfm7e, ((uint8_t *)&t10), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t11.code, t11.env, ccx12, gm_s6t4r831mukdr, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t14 = F.l2;
        if (!t14) goto ie13;
        {
            gm_t9qkh0rsooc8d t16;
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t17;
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
            gm_tftb1odc739t5 t18 = t17;
            uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
            F.ret = t17;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_b0_1 t20;
        gm_tftb1odc739t5 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        ((uint8_t *)&t21)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t21 + 1), t20);
        gm_tftb1odc739t5 t22 = t21;
        uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
        F.ret = t21;
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uuia5cm_c(void * a0, gm_tq3m5qgvkpje8 a1) { return gm_f_write_uuia5cm(a0, &a1); }
