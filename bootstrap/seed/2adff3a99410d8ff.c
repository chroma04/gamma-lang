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
typedef struct gm_cs3cuou245tr5f { _Bool f0; _Bool f1; _Bool f2; } gm_cs3cuou245tr5f;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
_Static_assert(sizeof(gm_cs3cuou245tr5f) == 3 && _Alignof(gm_cs3cuou245tr5f) == 1 && offsetof(gm_cs3cuou245tr5f, f0) == 0 && offsetof(gm_cs3cuou245tr5f, f1) == 1 && offsetof(gm_cs3cuou245tr5f, f2) == 2, "layout of `Restr`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tl874t2atnlh3 v1; } f1; } gm_t0u0j6voh3loe;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v1; } f1; } gm_tt7ek1a8bopcl;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0u0j6voh3loe) == 16 && _Alignof(gm_t0u0j6voh3loe) == 4 && offsetof(gm_t0u0j6voh3loe, f0) == 0 && offsetof(gm_t0u0j6voh3loe, f1) == 4, "layout of `Abort!Span`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v1; } f1; } gm_t4jn8fl3lmk5k;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf2orqcrphmek v1; } f1; } gm_t8u9q1tmoidhk;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; void * v1; } f1; } gm_tapdr2ag17fh7;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_th2lqbo77pvsc v1; } f1; } gm_thk954m1ol1ig;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v1; } f1; } gm_toc4mrkn2e7o9;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tpv68m88f9lcr;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tt7ek1a8bopcl) == 24 && _Alignof(gm_tt7ek1a8bopcl) == 8 && offsetof(gm_tt7ek1a8bopcl, f0) == 0 && offsetof(gm_tt7ek1a8bopcl, f1) == 8, "layout of `Abort![]Attribute`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
typedef struct { gm_tlivgedjs5ps3 f0; gm_b16_8 f1; gm_tapdr2ag17fh7 f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t0kv05e068jrn;
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4jn8fl3lmk5k) == 40 && _Alignof(gm_t4jn8fl3lmk5k) == 8 && offsetof(gm_t4jn8fl3lmk5k, f0) == 0 && offsetof(gm_t4jn8fl3lmk5k, f1) == 8, "layout of `Abort!Ident`");
_Static_assert(sizeof(gm_t8u9q1tmoidhk) == 64 && _Alignof(gm_t8u9q1tmoidhk) == 8 && offsetof(gm_t8u9q1tmoidhk, f0) == 0 && offsetof(gm_t8u9q1tmoidhk, f1) == 8, "layout of `Abort!Param`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; gm_t3fcbuehh3bs2 f2; gm_t3fcbuehh3bs2 f3; gm_t3fcbuehh3bs2 f4; } gm_tak1k6bk5qcen;
_Static_assert(sizeof(gm_tapdr2ag17fh7) == 48 && _Alignof(gm_tapdr2ag17fh7) == 8 && offsetof(gm_tapdr2ag17fh7, f0) == 0 && offsetof(gm_tapdr2ag17fh7, f1) == 8, "layout of `WhenElse`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tlivgedjs5ps3 v1; } f1; } gm_tdmdfjrg7d59n;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_thk954m1ol1ig) == 32 && _Alignof(gm_thk954m1ol1ig) == 8 && offsetof(gm_thk954m1ol1ig, f0) == 0 && offsetof(gm_thk954m1ol1ig, f1) == 8, "layout of `Abort!CallArgs`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tapdr2ag17fh7 v1; } f1; } gm_tihhqadak7gf3;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_toc4mrkn2e7o9) == 48 && _Alignof(gm_toc4mrkn2e7o9) == 8 && offsetof(gm_toc4mrkn2e7o9, f0) == 0 && offsetof(gm_toc4mrkn2e7o9, f1) == 8, "layout of `Abort!Block`");
_Static_assert(sizeof(gm_tpv68m88f9lcr) == 56 && _Alignof(gm_tpv68m88f9lcr) == 8 && offsetof(gm_tpv68m88f9lcr, f0) == 0 && offsetof(gm_tpv68m88f9lcr, f1) == 32 && offsetof(gm_tpv68m88f9lcr, f2) == 40, "layout of `Constraint`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_t0kv05e068jrn) == 152 && _Alignof(gm_t0kv05e068jrn) == 8 && offsetof(gm_t0kv05e068jrn, f0) == 0 && offsetof(gm_t0kv05e068jrn, f1) == 72 && offsetof(gm_t0kv05e068jrn, f2) == 88 && offsetof(gm_t0kv05e068jrn, f3) == 136, "layout of `FnLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_tak1k6bk5qcen) == 160 && _Alignof(gm_tak1k6bk5qcen) == 8 && offsetof(gm_tak1k6bk5qcen, f0) == 0 && offsetof(gm_tak1k6bk5qcen, f1) == 32 && offsetof(gm_tak1k6bk5qcen, f2) == 64 && offsetof(gm_tak1k6bk5qcen, f3) == 96 && offsetof(gm_tak1k6bk5qcen, f4) == 128, "layout of `(TokenKind, TokenKind, TokenKind, TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tdmdfjrg7d59n) == 80 && _Alignof(gm_tdmdfjrg7d59n) == 8 && offsetof(gm_tdmdfjrg7d59n, f0) == 0 && offsetof(gm_tdmdfjrg7d59n, f1) == 8, "layout of `Abort!FnSig`");
_Static_assert(sizeof(gm_tihhqadak7gf3) == 56 && _Alignof(gm_tihhqadak7gf3) == 8 && offsetof(gm_tihhqadak7gf3, f0) == 0 && offsetof(gm_tihhqadak7gf3, f1) == 8, "layout of `Abort!FnBody`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v1; } f1; } gm_to1cssm8dfiv5;
_Static_assert(sizeof(gm_to1cssm8dfiv5) == 96 && _Alignof(gm_to1cssm8dfiv5) == 8 && offsetof(gm_to1cssm8dfiv5, f0) == 0 && offsetof(gm_to1cssm8dfiv5, f1) == 8, "layout of `Abort!Expr`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_at_54906rm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_kw_p0r3sr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_newline_94n732e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_word_18dnh5f {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_box_j4urmed {
    gm_frame h;
    gm_tc3oht4ksth9d l0; /* e */
    void * ret;
};
struct gm_fr_eat_const_0ntbpcs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_eat_qmock9g {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_3jt8vgk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_65q32rn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_atlcs4j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ito0pap {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fail_e5kqpus {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_sig_to_type_3r7l7ih {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* params */
    gm_tf2orqcrphmek l5; /* param */
    gm_b0_1 ret;
};
struct gm_fr_kind_k239kpg {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_leave_rb20u32 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_18sm81j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_3t77g3a {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5kg5cjm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_mho6jfm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lo_fp82gt4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_mk_expr_53qj7nd {
    gm_frame h;
    gm_t2gtbh39ptekd l1; /* k */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_next_id_a7upaf4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_nth_c0r2nvs {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_nth_is_3fgqmcb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_parse_attr_args_16rponu {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* args */
    gm_tt7ek1a8bopcl ret;
};
struct gm_fr_parse_attributed_00u9rjp {
    gm_frame h;
    gm_tc3oht4ksth9d l4; /* inner */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_attrs_imvfdlq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* attrs */
    gm_tm4pc0emhrv1g l3; /* t */
    gm_tt7ek1a8bopcl ret;
};
struct gm_fr_parse_cont_type_ljmco4q {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* t */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_fn_body_09mp5ir {
    gm_frame h;
    gm_tihhqadak7gf3 ret;
};
struct gm_fr_parse_fn_expr_c9357a4 {
    gm_frame h;
    gm_tlivgedjs5ps3 l2; /* sig */
    gm_tf2orqcrphmek l7; /* param */
    gm_trqfvmjtt4sij l8; /* saved */
    gm_tihhqadak7gf3 l9; /* body */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_fn_sig_fecr4sl {
    gm_frame h;
    gm_tdmdfjrg7d59n ret;
};
struct gm_fr_parse_fn_type_only_0ifghqh {
    gm_frame h;
    gm_tlivgedjs5ps3 l2; /* sig */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_fn_type_sig_jrv5v82 {
    gm_frame h;
    gm_tlivgedjs5ps3 l2; /* sig */
    gm_tdmdfjrg7d59n ret;
};
struct gm_fr_parse_needs_opt_8lp1u45 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* effects */
    gm_thk954m1ol1ig ret;
};
struct gm_fr_parse_param_group_ac8kpvk {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* names */
    gm_t8u9q1tmoidhk ret;
};
struct gm_fr_parse_params_3g43t5f {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* params */
    gm_thk954m1ol1ig ret;
};
struct gm_fr_parse_prefix_type_1rn6mn8 {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_type_2m93dtq {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* lhs */
    gm_tc3oht4ksth9d l2; /* ok */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_type_name_d0ioo05 {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* e */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_type_operand_7guq29a {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_type_prefix_kfrfcdd {
    gm_frame h;
    gm_tc3oht4ksth9d l3; /* inner */
    gm_tc3oht4ksth9d l5; /* inner */
    gm_tc3oht4ksth9d l7; /* inner */
    gm_tc3oht4ksth9d l9; /* inner */
    gm_tc3oht4ksth9d l10; /* elem */
    gm_t4gut2c9fj1kt l12; /* d */
    gm_tc3oht4ksth9d l13; /* elem */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_where_opt_a5p2ja6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_tt7ek1a8bopcl ret;
};
struct gm_fr_report_djvi2hf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_set_restr_p85qqmv {
    gm_frame h;
    gm_toh0ootodhjue ret;
};
struct gm_fr_skip_newlines_0ap7sq2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_span_bm4ll8d {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_from_1sqap0o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_tok_ai6sc12 {
    gm_frame h;
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b);
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b);
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_kw_p0r3sr9(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_newline_94n732e(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_at_word_18dnh5f(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_bump_q2i40re(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_eat_const_0ntbpcs(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_65q32rn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_enter_5bf793b(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_ijgfrs1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_kw_7kbmp0i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_expected_5famfb9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fail_diag_u0q4kp3(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fail_e5kqpus(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_sig_to_type_3r7l7ih(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_leave_rb20u32(void * a0);
inline __attribute__((aligned(4))) void gm_f_list_18sm81j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_3t77g3a(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5kg5cjm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_mho6jfm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0);
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_48hbqcm(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_cnt7p5u(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0);
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) uint8_t gm_f_nth_is_name_oqhoobd(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_attr_args_16rponu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_attributed_00u9rjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_parse_attrs_imvfdlq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_block_tpg3f1i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_call_args_32e205h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_cont_type_ljmco4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_effect_lit_cqei081(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_expr_bkb2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_expr_in_45seo3h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_toh0ootodhjue a1);
__attribute__((aligned(4))) void gm_f_parse_fn_body_09mp5ir(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_fn_expr_c9357a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_fn_sig_fecr4sl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_fn_type_only_0ifghqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_fn_type_sig_jrv5v82(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_ident_n2c6b75(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_intrinsic_4ou54h3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_name_figjvbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_needs_opt_8lp1u45(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_param_group_ac8kpvk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_params_3g43t5f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_paren_sq1guoi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_parse_postfix_ao4kstr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_prefix_type_1rn6mn8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_struct_lit_86pp62c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_2m93dtq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_in_bvi7uav(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_toh0ootodhjue a1);
__attribute__((aligned(4))) void gm_f_parse_type_name_d0ioo05(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_operand_7guq29a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_parse_type_prefix_kfrfcdd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_parse_union_lit_39ap2o5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_where_opt_a5p2ja6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_9cfi95o(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_aqjeqnr(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s5rtia7(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vbfukt4(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_djvi2hf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1);
inline __attribute__((aligned(4))) void gm_f_skip_newlines_0ap7sq2(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0);
extern uint8_t gm_kakc8s3pa9o[40] __attribute__((aligned(8)));
extern uint8_t gm_kdpnk3k16op[40] __attribute__((aligned(8)));
extern uint8_t gm_kh832np8pgs[40] __attribute__((aligned(8)));
extern uint8_t gm_khouror17c2[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1pvh8pfrs3ir[7];
extern const uint8_t gm_s35hbb2rhhdvv[33];
extern const uint8_t gm_s8dujv4dnroq0[24];
extern const uint8_t gm_s9rm2mbmo3nku[7];
extern const uint8_t gm_s9tr38mqso4n7[93];
extern const uint8_t gm_sa8mtcad5rfmf[26];
extern const uint8_t gm_sandsuergllg3[30];
extern const uint8_t gm_sc71n5mgucblh[50];
extern const uint8_t gm_se7kda46rm021[107];
extern const uint8_t gm_sh7ob55fi0i62[26];
extern const uint8_t gm_shv4avtqvfr4p[6];
extern const uint8_t gm_skcto6psrjh96[28];
extern const uint8_t gm_slgf61fmeeof0[28];
extern const uint8_t gm_smglbbtmdmip2[6];
extern const uint8_t gm_smu8iti9fc903[85];
extern const uint8_t gm_spferj7kepn49[115];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcp8o48ld4n4jq[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuiab49hlgv7g[];
extern const uint8_t gm_sri0h4dm5d3ul[17];
extern const uint8_t gm_st2dnokm1h0jd[5];
extern const uint8_t gm_st803nqm7iok0[17];
extern const uint8_t gm_stugmrojdno85[6];
extern const uint8_t gm_suatl193icom4[41];
extern const uint8_t gm_sud6d85d454o1[42];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b) {
    if (!gm_eqbihgc69jv3(a + 0, b + 0)) return false;
    if (!gm_eqk7n1cdvnss(a + 16, b + 16)) return false;
    return true;
}
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)2) return gm_eq2ceic3f8rp(a + 8, b + 8);
    if (ta == (uint8_t)3) return gm_eqrv7v3j3o54(a + 8, b + 8);
    if (ta == (uint8_t)4) return gm_eqpa2btif7eh(a + 8, b + 8);
    if (ta == (uint8_t)5) return gm_eqbdg26q4aml(a + 8, b + 8);
    if (ta == (uint8_t)6) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)7) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)8) return gm_eqv3tj1pu43g(a + 8, b + 8);
    return true;
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 80);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcgfglnv8p059r, 96, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_54906rm F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    gm_t3fcbuehh3bs2 t3 = t2;
    gm_t9qkh0rsooc8d t4 = gm_l1;
    gm_t9qkh0rsooc8d t5 = t4;
    uint8_t t6 = (((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t3) + 8), ((uint8_t *)&t5))));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_kw_p0r3sr9(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_kw_p0r3sr9 F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    gm_t3fcbuehh3bs2 t3 = t2;
    gm_t9qkh0rsooc8d t4 = gm_l1;
    gm_t9qkh0rsooc8d t5 = t4;
    uint8_t t6 = (((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)2ULL)) && gm_eq2ceic3f8rp((((uint8_t *)&t3) + 8), ((uint8_t *)&t5))));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_newline_94n732e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_newline_94n732e F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    gm_t3fcbuehh3bs2 t3 = t2;
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)9ULL)));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_word_18dnh5f(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_word_18dnh5f F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* word */
    gm_b16_8 gm_l2; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 197u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_nth_c0r2nvs((void *)&t2, t1, ((uint64_t)0ULL));
    gm_t3fcbuehh3bs2 t3 = t2;
    uint8_t t4;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t7 != ((uint8_t)0ULL)) goto mn6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
    gm_l2 = t8;
    gm_b16_8 t9 = gm_l2;
    gm_b16_8 t10 = gm_l1;
    gm_b16_8 t11 = t9;
    gm_b16_8 t12 = t10;
    uint8_t t13 = (gm_eq0iqiubfm7e(((uint8_t *)&t11), ((uint8_t *)&t12)));
    t4 = t13;
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_srcuiab49hlgv7g, 170, 58);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_box_j4urmed F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju t1 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdpnk3k16op)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t2 = t1;
    if (((((uint8_t *)&t2))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t2) + 8, gm_srcuiab49hlgv7g, 452, 52);
    void * t3 = GM_LD(void *, (((uint8_t *)&t2) + 8));
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
inline __attribute__((aligned(4))) uint8_t gm_f_eat_const_0ntbpcs(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eat_const_0ntbpcs F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_stugmrojdno85));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
        uint8_t t5 = gm_f_at_word_18dnh5f(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
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
        {
            void * t10 = gm_l0;
            gm_tm4pc0emhrv1g t11;
            gm_f_bump_q2i40re((void *)&t11, t10);
        }
    }
    {
        uint8_t t12 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eat_qmock9g F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4 = gm_l1;
        uint8_t t5 = gm_f_at_54906rm(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
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
        {
            void * t10 = gm_l0;
            gm_tm4pc0emhrv1g t11;
            gm_f_bump_q2i40re((void *)&t11, t10);
        }
    }
    {
        uint8_t t12 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_65q32rn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_65q32rn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_atlcs4j F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 72)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ito0pap F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fail_e5kqpus(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_fail_e5kqpus F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_l2;
    gm_t4gut2c9fj1kt t4;
    gm_f_error_s6hn871((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t5 = gm_f_fail_diag_u0q4kp3(t1, &t4);
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
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_k239kpg F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_f_tok_ai6sc12(t1);
    gm_t3fcbuehh3bs2 t3 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)t2) + 0));
    F.ret = t3;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_leave_rb20u32(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_leave_rb20u32 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 132));
        uint32_t r5; bool o6 = __builtin_sub_overflow(t4, ((uint32_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 434, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 132), t7);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_18sm81j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_18sm81j F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_atlcs4j();
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
inline __attribute__((aligned(4))) void gm_f_list_3t77g3a(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_3t77g3a F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ito0pap();
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
inline __attribute__((aligned(4))) void gm_f_list_5kg5cjm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5kg5cjm F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3jt8vgk();
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
inline __attribute__((aligned(4))) void gm_f_list_mho6jfm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_mho6jfm F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_65q32rn();
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_g3tddrb();
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
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lo_fp82gt4 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t1));
    gm_tl874t2atnlh3 t3 = t2;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&t3) + 4));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mk_expr_53qj7nd F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l2; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = GM_CONV(gm_tl874t2atnlh3, a2);
    memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 4, 0, 4);
    memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 84, 0, 4);
    void * t1 = gm_l0;
    uint32_t t2 = gm_f_next_id_a7upaf4(t1);
    gm_t2gtbh39ptekd t3 = F.l1;
    gm_tl874t2atnlh3 t4 = gm_l2;
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 0, t2);
    GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 8, t3);
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 72, t4);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_id_a7upaf4 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* id */
    void * gm_l2; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 56));
        uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)t3));
        gm_l1 = t4;
    }
    {
        {
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, (((uint8_t *)t6) + 56));
            void * t8 = (void *)((uint8_t *)t7);
            gm_l2 = t8;
        }
        {
            void * t9 = gm_l2;
            void * t10 = gm_l2;
            uint32_t t11 = GM_LD(uint32_t, ((uint8_t *)t10));
            uint32_t r12; bool o13 = __builtin_add_overflow(t11, ((uint32_t)1ULL), &r12);
            if (o13) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 443, 5);
            uint32_t t14 = r12;
            GM_ST(uint32_t, ((uint8_t *)t9), t14);
        }
    }
    {
        uint32_t t15 = gm_l1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_c0r2nvs F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_b16_8 t3 = t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 72));
    uint64_t t6 = gm_l1;
    uint64_t r7; bool o8 = __builtin_add_overflow(t5, t6, &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 126, 70);
    uint64_t t9 = r7;
    void * t10 = gm_l0;
    uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 8) + 8);
    uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
    if (o13) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 126, 85);
    uint64_t t14 = r12;
    uint64_t t15 = (t9 < t14 ? t9 : t14);
    uint64_t t16 = (uintptr_t)t15;
    gm_b16_8 t17 = t3;
    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&t17) + 8);
    if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcuiab49hlgv7g, 126, 62);
    gm_t3fcbuehh3bs2 t19 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t17)) + t16 * 48) + 0));
    F.ret = t19;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_is_3fgqmcb F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint64_t t2 = gm_l1;
    gm_t3fcbuehh3bs2 t3;
    gm_f_nth_c0r2nvs((void *)&t3, t1, t2);
    gm_t3fcbuehh3bs2 t4 = t3;
    gm_t9qkh0rsooc8d t5 = gm_l2;
    gm_t9qkh0rsooc8d t6 = t5;
    uint8_t t7 = (((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t4) + 8), ((uint8_t *)&t6))));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_djvi2hf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_report_djvi2hf F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 136));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 253, 5);
        uint64_t t7 = r5;
        GM_ST(uint64_t, (((uint8_t *)t2) + 136), t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 64));
        gm_tl874t2atnlh3 t10 = gm_l1;
        gm_b16_8 t11 = gm_l2;
        gm_b0_1 t12 = gm_f_report_eardf07((void *)NULL, cx, NULL, t9, t10, t11);
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
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_set_restr_p85qqmv F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* r */
    gm_toh0ootodhjue gm_l2; /* old */
    GM_STACK_CHECK_SHALLOW(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_toh0ootodhjue, a1);
    {
        void * t2 = gm_l0;
        gm_toh0ootodhjue t3 = GM_LD(gm_toh0ootodhjue, (((uint8_t *)t2) + 128));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_toh0ootodhjue t5 = gm_l1;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t4) + 128), t5);
    }
    {
        gm_toh0ootodhjue t6 = gm_l2;
        uint8_t t7 = 0;
        F.ret = t6;
        goto gm_fret;
    }
gm_fret:
    return GM_CONV(gm_cs3cuou245tr5f, F.ret);
gm_fprop:
    return GM_CONV(gm_cs3cuou245tr5f, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_skip_newlines_0ap7sq2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_newlines_0ap7sq2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t3 = gm_l0;
            uint8_t t4 = gm_f_at_newline_94n732e(t3);
            if (!t4) goto lb2;
            {
                {
                    void * t8 = gm_l0;
                    gm_tm4pc0emhrv1g t9;
                    gm_f_bump_q2i40re((void *)&t9, t8);
                }
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_bm4ll8d F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_f_tok_ai6sc12(t1);
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 32));
    F.ret = t3;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_from_1sqap0o F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)t1) + 0));
    uint32_t t3 = gm_l1;
    void * t4 = gm_l0;
    uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 80));
    uint32_t t6 = gm_l1;
    uint32_t t7 = (t5 > t6 ? t5 : t6);
    gm_tl874t2atnlh3 t8;
    GM_ST(uint32_t, ((uint8_t *)&t8 + 0), t2);
    GM_ST(uint32_t, ((uint8_t *)&t8 + 4), t3);
    GM_ST(uint32_t, ((uint8_t *)&t8 + 8), t7);
    F.ret = t8;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tok_ai6sc12 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_b16_8 t3 = t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 72));
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 8) + 8);
    uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)1ULL), &r8);
    if (o9) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 122, 73);
    uint64_t t10 = r8;
    uint64_t t11 = (t5 < t10 ? t5 : t10);
    uint64_t t12 = (uintptr_t)t11;
    gm_b16_8 t13 = t3;
    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&t13) + 8);
    if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srcuiab49hlgv7g, 122, 56);
    void * t15 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t13)) + t12 * 48);
    F.ret = t15;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_eat_const_0ntbpcs(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_sig_to_type_3r7l7ih(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_sig_to_type_3r7l7ih F;
    void * gm_l0; /* p */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t08olq97rscn3 gm_l8; /* n */
    GM_STACK_CHECK(&F, 725u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_3t77g3a((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l1;
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
                    gm_tf2orqcrphmek t16 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 56));
                    F.l5 = t16;
                    {
                        void * t19 = GM_LD(void *, (((uint8_t *)&F.l5) + 24));
                        void * t20 = t19;
                        uint8_t t21;
                        if ((GM_LD(void *, ((uint8_t *)&t20)) != 0)) goto mn23;
                        t21 = ((uint8_t)1);
                        goto me22;
mn23:;
                        t21 = ((uint8_t)0);
                        goto me22;
mn24:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 238, 12);
me22:;
                        if (!t21) goto ie18;
                        void * t25 = GM_LD(void *, (((uint8_t *)&F.l5) + 32));
                        void * t26 = t25;
                        uint8_t t27;
                        if ((GM_LD(void *, ((uint8_t *)&t26)) != 0)) goto mn29;
                        t27 = ((uint8_t)1);
                        goto me28;
mn29:;
                        t27 = ((uint8_t)0);
                        goto me28;
mn30:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 238, 33);
me28:;
                        if (!t27) goto ie18;
                        uint64_t t31 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                        uint8_t t32 = (t31 > ((uint64_t)0ULL));
                        if (!t32) goto ie18;
                        {
                            {
                                gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                                gm_l6 = t35;
                            }
                            {
                                gm_l7 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t37 = gm_l7;
                                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    uint8_t t39 = (t37 < t38);
                                    if (!t39) goto lb36;
                                    {
                                        uint64_t t42 = gm_l7;
                                        uint64_t t43 = (uintptr_t)t42;
                                        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                        gm_t08olq97rscn3 t47 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t43 * 32));
                                        gm_l8 = t47;
                                        {
                                            void * t49 = (void *)((uint8_t *)&F.l2);
                                            gm_tf2orqcrphmek t50;
                                            memset((uint8_t *)&t50 + 18, 0, 6);
                                            memset((uint8_t *)&t50 + 52, 0, 4);
                                            gm_b16_8 t51 = gm_f_empty_slice_65q32rn();
                                            GM_ST(gm_b16_8, (uint8_t *)&t50 + 0, t51);
                                            GM_ST(uint8_t, (uint8_t *)&t50 + 16, ((uint8_t)0));
                                            GM_ST(uint8_t, (uint8_t *)&t50 + 17, ((uint8_t)0));
                                            void * t52 = gm_l0;
                                            gm_t08olq97rscn3 t53 = gm_l8;
                                            gm_t2gtbh39ptekd t54;
                                            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t54 + 40, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t54 + 48, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t54 + 56, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)6ULL));
                                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t54 + 8), t53);
                                            gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l8) + 16));
                                            gm_tc3oht4ksth9d t56;
                                            gm_f_mk_expr_53qj7nd((void *)&t56, t52, &t54, GM_CONV(gm_cseahvg8v7p0vs, t55));
                                            void * t57 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t56);
                                            if (gm_unw.kind) {
                                                goto gm_ll40;
                                            }
                                            GM_ST(void *, (uint8_t *)&t50 + 24, t57);
                                            GM_ST(void *, (uint8_t *)&t50 + 32, ((void *)0));
                                            gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l8) + 16));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t50 + 40, t58);
                                            gm_tftb1odc739t5 t59 = gm_f_push_vbfukt4(t49, &t50);
                                            gm_tftb1odc739t5 t60 = t59;
                                            if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_srcp8o48ld4n4jq, 240, 17);
                                            gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
                                        }
                                        goto me45;
mn46:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 239, 13);
me45:;
                                    }
lc36:;
                                    {
                                        uint64_t t63 = gm_l7;
                                        uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                                        uint64_t t66 = r64;
                                        gm_l7 = t66;
                                    }
                                }
gm_ll40:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb36; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc36; }
                                }
                                goto gm_ll9;
lb36:;
                            }
                        }
                        goto ix18;
ie18:;
                        {
                            void * t68 = (void *)((uint8_t *)&F.l2);
                            gm_tftb1odc739t5 t69 = gm_f_push_vbfukt4(t68, &(*(gm_tf2orqcrphmek *)((uint8_t *)&F.l5)));
                            gm_tftb1odc739t5 t70 = t69;
                            if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_srcp8o48ld4n4jq, 250, 13);
                            gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
                        }
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 237, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t73 = gm_l4;
                    uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                    uint64_t t76 = r74;
                    gm_l4 = t76;
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
        void * t77 = gm_l1;
        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)t77) + 0), t78);
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
inline __attribute__((aligned(4))) void gm_f_parse_attr_args_16rponu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_attr_args_16rponu F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    GM_STACK_CHECK(&F, 655u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            gm_t9qkh0rsooc8d t4;
            GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)61ULL));
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(0));
            gm_t0u0j6voh3loe t6 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t3, t4, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t7 = t6;
            if (((((uint8_t *)&t7))[0] != 0)) {
                gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 4));
                gm_tt7ek1a8bopcl t9;
                GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t9 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
                ((uint8_t *)&t9)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t9 + 8), t8);
                F.ret = t9;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t7) + 4));
        }
    }
    {
        void * t11 = gm_l0;
        gm_toh0ootodhjue t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t12 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t12 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t13 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t11, GM_CONV(gm_cs3cuou245tr5f, t12)));
        gm_l1 = t13;
    }
    {
    }
    {
        gm_f_list_vp1djv1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        for (;;) {
            void * t18 = gm_l0;
            gm_t9qkh0rsooc8d t19;
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)62ULL));
            uint8_t t20 = gm_f_at_54906rm(t18, t19);
            uint8_t t21 = (uint8_t)!t20;
            if (!t21) goto lb17;
            {
                void * t24 = (void *)((uint8_t *)&F.l2);
                void * t25 = gm_l0;
                gm_to1cssm8dfiv5 t26;
                gm_f_parse_expr_bkb2n6e((void *)&t26, (void *)NULL, cx, NULL, t25);
                if (gm_unw.kind) {
                    goto gm_ll22;
                }
                gm_to1cssm8dfiv5 t27 = t26;
                if (((((uint8_t *)&t27))[0] != 0)) {
                    gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 8));
                    gm_tt7ek1a8bopcl t29;
                    GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t29 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                    ((uint8_t *)&t29)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t29 + 8), t28);
                    F.ret = t29;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ll22;
                }
                gm_tc3oht4ksth9d t30 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t27) + 8));
                gm_tftb1odc739t5 t31 = gm_f_push_d943vgp(t24, &t30);
                gm_tftb1odc739t5 t32 = t31;
                if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srcp8o48ld4n4jq, 164, 9);
                gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
            }
            {
                void * t35 = gm_l0;
                gm_t9qkh0rsooc8d t36;
                GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)57ULL));
                uint8_t t37 = gm_f_eat_qmock9g(t35, t36);
                uint8_t t38 = (uint8_t)!t37;
                if (!t38) goto ie34;
                {
                    goto lb17;
                }
                goto ix34;
ie34:;
ix34:;
            }
lc17:;
        }
gm_ll22:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_ld1_1;
lb17:;
    }
    {
        {
            void * t41 = gm_l0;
            gm_t9qkh0rsooc8d t42;
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)62ULL));
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s35hbb2rhhdvv));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(32));
            gm_t0u0j6voh3loe t44 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t41, t42, t43);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_t0u0j6voh3loe t45 = t44;
            if (((((uint8_t *)&t45))[0] != 0)) {
                gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 4));
                gm_tt7ek1a8bopcl t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t47 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t47 + 16, 0);
                ((uint8_t *)&t47)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t47 + 8), t46);
                F.ret = t47;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld1_1;
            }
            gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t45) + 4));
        }
    }
    {
        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_tt7ek1a8bopcl t50;
        GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
        ((uint8_t *)&t50)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 8), t49);
        gm_tt7ek1a8bopcl t51 = t50;
        uint8_t t52 = ((((uint8_t *)&t51))[0] != 0);
        F.ret = t50;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t52;
        goto gm_ld1_1;
    }
    {
        {
            void * t54 = gm_l0;
            gm_toh0ootodhjue t55 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t54) + 128), t55);
        }
    }
    goto gm_ls56;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t58 = gm_l0;
            gm_toh0ootodhjue t59 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t58) + 128), t59);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls56:;
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
inline __attribute__((aligned(4))) void gm_f_parse_attributed_00u9rjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_parse_attributed_00u9rjp F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* mode */
    uint32_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* attrs */
    GM_STACK_CHECK(&F, 1019u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_tt7ek1a8bopcl t5;
        gm_f_parse_attrs_imvfdlq((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tt7ek1a8bopcl t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 8));
            gm_to1cssm8dfiv5 t8;
            memset(&t8, 0, sizeof t8);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), t7);
            F.ret = t8;
            goto gm_fret;
        }
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l3 = t9;
    }
    {
        void * t10 = gm_l0;
        gm_t9qkh0rsooc8d t11 = gm_l1;
        gm_to1cssm8dfiv5 t12;
        gm_f_parse_type_operand_7guq29a((void *)&t12, (void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) {
            gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 8));
            gm_to1cssm8dfiv5 t15;
            memset(&t15, 0, sizeof t15);
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t15 + 8), t14);
            F.ret = t15;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t16 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t13) + 8));
        F.l4 = t16;
    }
    {
        void * t17 = gm_l0;
        gm_tpe1hv3fekn05 t18;
        gm_b16_8 t19 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
        void * t20 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t18 + 16, t20);
        gm_t2gtbh39ptekd t21;
        memset(&t21, 0, sizeof t21);
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)35ULL));
        GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t21 + 8), t18);
        void * t22 = gm_l0;
        uint32_t t23 = gm_l2;
        gm_tl874t2atnlh3 t24 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t22, t23));
        gm_tc3oht4ksth9d t25;
        gm_f_mk_expr_53qj7nd((void *)&t25, t17, &t21, GM_CONV(gm_cseahvg8v7p0vs, t24));
        gm_to1cssm8dfiv5 t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
        ((uint8_t *)&t26)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t26 + 8), t25);
        gm_to1cssm8dfiv5 t27 = t26;
        uint8_t t28 = ((((uint8_t *)&t27))[0] != 0);
        F.ret = t26;
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
inline __attribute__((aligned(4))) void gm_f_parse_attrs_imvfdlq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_attrs_imvfdlq F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l2; /* sym */
    gm_th2lqbo77pvsc gm_l4; /* args */
    GM_STACK_CHECK(&F, 508u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_18sm81j((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            void * t3 = gm_l0;
            gm_t3fcbuehh3bs2 t4;
            gm_f_kind_k239kpg((void *)&t4, t3);
            gm_t3fcbuehh3bs2 t5 = t4;
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t6 != ((uint8_t)7ULL)) goto lb2;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
            gm_l2 = t7;
            {
                void * t10 = gm_l0;
                gm_f_bump_q2i40re((void *)&F.l3, t10);
            }
            {
                gm_th2lqbo77pvsc t11;
                memset(&t11, 0, sizeof t11);
                gm_l4 = t11;
            }
            {
                void * t13 = gm_l0;
                gm_t9qkh0rsooc8d t14;
                GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)61ULL));
                uint8_t t15 = gm_f_at_54906rm(t13, t14);
                if (!t15) goto ie12;
                {
                    void * t17 = gm_l0;
                    gm_tt7ek1a8bopcl t18;
                    gm_f_parse_attr_args_16rponu((void *)&t18, (void *)NULL, cx, NULL, t17);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                    gm_tt7ek1a8bopcl t19 = t18;
                    if (((((uint8_t *)&t19))[0] != 0)) {
                        gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 8));
                        gm_tt7ek1a8bopcl t21;
                        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t21 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t21 + 16, 0);
                        ((uint8_t *)&t21)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t21 + 8), t20);
                        F.ret = t21;
                        goto gm_fret;
                    }
                    gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
                    gm_th2lqbo77pvsc t23;
                    GM_ST(uint8_t, (uint8_t *)&t23 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t23 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t23 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t23), t22);
                    ((uint8_t *)&t23)[16] = 1;
                    gm_l4 = t23;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                void * t24 = (void *)((uint8_t *)&F.l1);
                gm_tkacitepg5fk0 t25;
                memset((uint8_t *)&t25 + 68, 0, 4);
                memset((uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&t25 + 0)) + 28, 0, 4);
                gm_b16_8 t26 = gm_l2;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&t25 + 0)) + 0, t26);
                gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 32));
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&t25 + 0)) + 16, t27);
                memcpy((uint8_t *)&t25 + 32, ((uint8_t *)&gm_l4), 24);
                void * t28 = gm_l0;
                uint32_t t29 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 32) + 4));
                gm_tl874t2atnlh3 t30 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t28, t29));
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t25 + 56, t30);
                gm_tftb1odc739t5 t31 = gm_f_push_9cfi95o(t24, &t25);
                gm_tftb1odc739t5 t32 = t31;
                if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srcp8o48ld4n4jq, 152, 9);
                gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
            }
            {
                void * t34 = gm_l0;
                gm_f_skip_newlines_0ap7sq2(t34);
            }
lc2:;
        }
gm_ll8:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_tt7ek1a8bopcl t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t36 + 8), t35);
        gm_tt7ek1a8bopcl t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
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
inline __attribute__((aligned(4))) void gm_f_parse_cont_type_ljmco4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_cont_type_ljmco4q F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK(&F, 1073u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        {
            void * t5 = gm_l0;
            gm_tm4pc0emhrv1g t6;
            gm_f_bump_q2i40re((void *)&t6, t5);
        }
    }
    {
        {
            void * t8 = gm_l0;
            gm_tm4pc0emhrv1g t9;
            gm_f_bump_q2i40re((void *)&t9, t8);
        }
    }
    {
        void * t10 = gm_l0;
        gm_toh0ootodhjue t11;
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t11 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t11 + 2), ((uint8_t)0));
        gm_to1cssm8dfiv5 t12;
        gm_f_parse_type_in_bvi7uav((void *)&t12, (void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) {
            gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 8));
            gm_to1cssm8dfiv5 t15;
            memset(&t15, 0, sizeof t15);
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t15 + 8), t14);
            F.ret = t15;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t16 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t13) + 8));
        F.l2 = t16;
    }
    {
        {
            void * t18 = gm_l0;
            gm_t9qkh0rsooc8d t19;
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)62ULL));
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_st803nqm7iok0));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(16));
            gm_t0u0j6voh3loe t21 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t18, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) {
                gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 4));
                gm_to1cssm8dfiv5 t24;
                memset(&t24, 0, sizeof t24);
                ((uint8_t *)&t24)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t24 + 8), t23);
                F.ret = t24;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t22) + 4));
        }
    }
    {
        void * t26 = gm_l0;
        void * t27 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t2gtbh39ptekd t28;
        memset(&t28, 0, sizeof t28);
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)31ULL));
        GM_ST(void *, ((uint8_t *)&t28 + 8), t27);
        void * t29 = gm_l0;
        uint32_t t30 = gm_l1;
        gm_tl874t2atnlh3 t31 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t29, t30));
        gm_tc3oht4ksth9d t32;
        gm_f_mk_expr_53qj7nd((void *)&t32, t26, &t28, GM_CONV(gm_cseahvg8v7p0vs, t31));
        gm_to1cssm8dfiv5 t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
        ((uint8_t *)&t33)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t33 + 8), t32);
        gm_to1cssm8dfiv5 t34 = t33;
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t33;
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
inline __attribute__((aligned(4))) void gm_f_parse_fn_body_09mp5ir(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_fn_body_09mp5ir F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 1308u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)59ULL));
        uint8_t t5 = gm_f_eat_qmock9g(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            gm_toc4mrkn2e7o9 t9;
            gm_f_parse_block_tpg3f1i((void *)&t9, (void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_toc4mrkn2e7o9 t10 = t9;
            if (((((uint8_t *)&t10))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 8));
                gm_tihhqadak7gf3 t12;
                memset(&t12, 0, sizeof t12);
                ((uint8_t *)&t12)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t12 + 8), t11);
                F.ret = t12;
                goto gm_fret;
            }
            gm_t79rbdv4rafe5 t13 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&t10) + 8));
            gm_tapdr2ag17fh7 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t14 + 8), t13);
            gm_tihhqadak7gf3 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_tapdr2ag17fh7, ((uint8_t *)&t15 + 8), t14);
            gm_tihhqadak7gf3 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l0;
        gm_t9qkh0rsooc8d t20;
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)65ULL));
        uint8_t t21 = gm_f_at_54906rm(t19, t20);
        if (!t21) goto ie18;
        {
            void * t23 = gm_l0;
            void * t24 = gm_l0;
            gm_tl874t2atnlh3 t25 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t24));
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_smu8iti9fc903));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(84));
            gm_b0_1 t27 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t23, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t28 = gm_l0;
            gm_toc4mrkn2e7o9 t29;
            gm_f_parse_block_tpg3f1i((void *)&t29, (void *)NULL, cx, NULL, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_toc4mrkn2e7o9 t30 = t29;
            if (((((uint8_t *)&t30))[0] != 0)) {
                gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 8));
                gm_tihhqadak7gf3 t32;
                memset(&t32, 0, sizeof t32);
                ((uint8_t *)&t32)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t32 + 8), t31);
                F.ret = t32;
                goto gm_fret;
            }
            gm_t79rbdv4rafe5 t33 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&t30) + 8));
            gm_tapdr2ag17fh7 t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t34 + 8), t33);
            gm_tihhqadak7gf3 t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            ((uint8_t *)&t35)[0] = 0;
            GM_ST(gm_tapdr2ag17fh7, ((uint8_t *)&t35 + 8), t34);
            gm_tihhqadak7gf3 t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t38 = gm_l0;
        gm_to1cssm8dfiv5 t39;
        gm_f_parse_expr_bkb2n6e((void *)&t39, (void *)NULL, cx, NULL, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t40 = t39;
        if (((((uint8_t *)&t40))[0] != 0)) {
            gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 8));
            gm_tihhqadak7gf3 t42;
            memset(&t42, 0, sizeof t42);
            ((uint8_t *)&t42)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t42 + 8), t41);
            F.ret = t42;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t43 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t40) + 8));
        void * t44 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tapdr2ag17fh7 t45;
        memset(&t45, 0, sizeof t45);
        GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)1ULL));
        GM_ST(void *, ((uint8_t *)&t45 + 8), t44);
        gm_tihhqadak7gf3 t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
        ((uint8_t *)&t46)[0] = 0;
        GM_ST(gm_tapdr2ag17fh7, ((uint8_t *)&t46 + 8), t45);
        gm_tihhqadak7gf3 t47 = t46;
        uint8_t t48 = ((((uint8_t *)&t47))[0] != 0);
        F.ret = t46;
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
__attribute__((aligned(4))) void gm_f_parse_fn_expr_c9357a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_fn_expr_c9357a4 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_b16_8 gm_l3; /* where_ */
    uint8_t gm_l4; /* is_lit */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tl874t2atnlh3 gm_l10; /* whole */
    GM_STACK_CHECK(&F, 2433u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_tdmdfjrg7d59n t5;
        gm_f_parse_fn_sig_fecr4sl((void *)&t5, (void *)NULL, cx, NULL, t4, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tdmdfjrg7d59n t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 8));
            gm_to1cssm8dfiv5 t8;
            memset(&t8, 0, sizeof t8);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), t7);
            F.ret = t8;
            goto gm_fret;
        }
        gm_tlivgedjs5ps3 t9 = GM_LD(gm_tlivgedjs5ps3, (((uint8_t *)&t6) + 8));
        F.l2 = t9;
    }
    {
        void * t10 = gm_l0;
        gm_tt7ek1a8bopcl t11;
        gm_f_parse_where_opt_a5p2ja6((void *)&t11, (void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tt7ek1a8bopcl t12 = t11;
        if (((((uint8_t *)&t12))[0] != 0)) {
            gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 8));
            gm_to1cssm8dfiv5 t14;
            memset(&t14, 0, sizeof t14);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t14 + 8), t13);
            F.ret = t14;
            goto gm_fret;
        }
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
        gm_l3 = t15;
    }
    {
        uint8_t t16 = 0;
        void * t17 = gm_l0;
        gm_t9qkh0rsooc8d t18;
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)59ULL));
        uint8_t t19 = gm_f_at_54906rm(t17, t18);
        if (!t19) {
            uint8_t t20 = 0;
            void * t21 = gm_l0;
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)65ULL));
            uint8_t t23 = gm_f_at_54906rm(t21, t22);
            if (t23) {
                void * t24 = gm_l0;
                uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 128) + 0));
                uint8_t t26 = (uint8_t)!t25;
                t20 = t26;
            } else t20 = 0;
            t16 = t20;
        } else t16 = 1;
        gm_l4 = t16;
    }
    {
        uint8_t t28 = gm_l4;
        uint8_t t29 = (uint8_t)!t28;
        if (!t29) goto ie27;
        {
            uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t33 = (t32 > ((uint64_t)0ULL));
            if (!t33) goto ie31;
            {
                void * t35 = gm_l0;
                gm_b16_8 t36;
                GM_ST(void *, (uint8_t *)&t36, (void *)(gm_suatl193icom4));
                GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(40));
                gm_b0_1 t37 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t35, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_to1cssm8dfiv5 t38;
                memset(&t38, 0, sizeof t38);
                ((uint8_t *)&t38)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t38 + 8), t37);
                gm_to1cssm8dfiv5 t39 = t38;
                uint8_t t40 = ((((uint8_t *)&t39))[0] != 0);
                F.ret = t38;
                goto gm_fret;
            }
            goto ix31;
ie31:;
ix31:;
        }
        {
            void * t41 = gm_l0;
            void * t42 = (void *)((uint8_t *)&F.l2);
            gm_b0_1 t43 = gm_f_fn_sig_to_type_3r7l7ih((void *)NULL, cx, NULL, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t44 = gm_l0;
            gm_tq7s98ihonkju t45 = gm_f_new_48hbqcm((void *)NULL, cx, NULL, &(*(gm_tlivgedjs5ps3 *)((uint8_t *)&F.l2)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_khouror17c2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t46 = t45;
            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_srcp8o48ld4n4jq, 210, 34);
            void * t47 = GM_LD(void *, (((uint8_t *)&t46) + 8));
            gm_t2gtbh39ptekd t48;
            memset(&t48, 0, sizeof t48);
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)30ULL));
            GM_ST(void *, ((uint8_t *)&t48 + 8), t47);
            void * t49 = gm_l0;
            uint32_t t50 = gm_l1;
            gm_tl874t2atnlh3 t51 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t49, t50));
            gm_tc3oht4ksth9d t52;
            gm_f_mk_expr_53qj7nd((void *)&t52, t44, &t48, GM_CONV(gm_cseahvg8v7p0vs, t51));
            gm_to1cssm8dfiv5 t53;
            GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
            ((uint8_t *)&t53)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t53 + 8), t52);
            gm_to1cssm8dfiv5 t54 = t53;
            uint8_t t55 = ((((uint8_t *)&t54))[0] != 0);
            F.ret = t53;
            goto gm_fret;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        {
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_l5 = t57;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t59 = gm_l6;
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t61 = (t59 < t60);
                if (!t61) goto lb58;
                {
                    uint64_t t64 = gm_l6;
                    uint64_t t65 = (uintptr_t)t64;
                    uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tf2orqcrphmek t69 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t65 * 56));
                    F.l7 = t69;
                    {
                        uint64_t t72 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                        uint8_t t73 = ((t72 == ((uint64_t)0ULL)));
                        if (!t73) goto ie71;
                        {
                            void * t75 = gm_l0;
                            gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l7) + 40));
                            gm_b16_8 t77;
                            GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sud6d85d454o1));
                            GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(41));
                            gm_b0_1 t78 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t75, t76, t77);
                            if (gm_unw.kind) {
                                goto gm_ll62;
                            }
                        }
                        goto ix71;
ie71:;
ix71:;
                    }
                    goto me67;
mn68:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 212, 5);
me67:;
                }
lc58:;
                {
                    uint64_t t80 = gm_l6;
                    uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                    uint64_t t83 = r81;
                    gm_l6 = t83;
                }
            }
gm_ll62:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb58; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc58; }
            }
            goto gm_lad;
lb58:;
        }
    }
    {
        void * t84 = gm_l0;
        gm_trqfvmjtt4sij t85 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t84) + 88));
        F.l8 = t85;
    }
    {
        void * t86 = gm_l0;
        uint8_t *t87 = (((uint8_t *)t86) + 88);
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)t87), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t88 = gm_l0;
        gm_f_parse_fn_body_09mp5ir((void *)&F.l9, (void *)NULL, cx, NULL, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t89 = gm_l0;
        gm_trqfvmjtt4sij t90 = F.l8;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t89) + 88), t90);
    }
    {
        void * t91 = gm_l0;
        uint32_t t92 = gm_l1;
        gm_tl874t2atnlh3 t93 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t91, t92));
        gm_l10 = t93;
    }
    {
        void * t94 = gm_l0;
        gm_t0kv05e068jrn t95;
        memset((uint8_t *)&t95 + 148, 0, 4);
        memcpy((uint8_t *)&t95 + 0, ((uint8_t *)&F.l2), 72);
        gm_b16_8 t96 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t95 + 72, t96);
        gm_tihhqadak7gf3 t97 = F.l9;
        gm_tihhqadak7gf3 t98 = t97;
        if (((((uint8_t *)&t98))[0] != 0)) {
            gm_b0_1 t99 = GM_LD(gm_b0_1, (((uint8_t *)&t98) + 8));
            gm_to1cssm8dfiv5 t100;
            memset(&t100, 0, sizeof t100);
            ((uint8_t *)&t100)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t100 + 8), t99);
            F.ret = t100;
            goto gm_fret;
        }
        gm_tapdr2ag17fh7 t101 = GM_LD(gm_tapdr2ag17fh7, (((uint8_t *)&t98) + 8));
        GM_ST(gm_tapdr2ag17fh7, (uint8_t *)&t95 + 88, t101);
        gm_tl874t2atnlh3 t102 = gm_l10;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t95 + 136, t102);
        gm_tq7s98ihonkju t103 = gm_f_new_cnt7p5u((void *)NULL, cx, NULL, &t95, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kakc8s3pa9o)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t104 = t103;
        if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 8, gm_srcp8o48ld4n4jq, 221, 26);
        void * t105 = GM_LD(void *, (((uint8_t *)&t104) + 8));
        gm_t2gtbh39ptekd t106;
        memset(&t106, 0, sizeof t106);
        GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)36ULL));
        GM_ST(void *, ((uint8_t *)&t106 + 8), t105);
        gm_tl874t2atnlh3 t107 = gm_l10;
        gm_tc3oht4ksth9d t108;
        gm_f_mk_expr_53qj7nd((void *)&t108, t94, &t106, GM_CONV(gm_cseahvg8v7p0vs, t107));
        gm_to1cssm8dfiv5 t109;
        GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
        ((uint8_t *)&t109)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t109 + 8), t108);
        gm_to1cssm8dfiv5 t110 = t109;
        uint8_t t111 = ((((uint8_t *)&t110))[0] != 0);
        F.ret = t109;
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
inline __attribute__((aligned(4))) void gm_f_parse_fn_sig_fecr4sl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_parse_fn_sig_fecr4sl F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* is_foreign */
    uint32_t gm_l2; /* start */
    gm_th2lqbo77pvsc gm_l3; /* params */
    void * gm_l4; /* ret */
    gm_th2lqbo77pvsc gm_l5; /* effects */
    GM_STACK_CHECK(&F, 1400u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l2 = t3;
    }
    {
        {
            void * t5 = gm_l0;
            gm_t9qkh0rsooc8d t6;
            GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)11ULL));
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(0));
            gm_t0u0j6voh3loe t8 = gm_f_expect_kw_7kbmp0i((void *)NULL, cx, NULL, t5, t6, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t9 = t8;
            if (((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 4));
                gm_tdmdfjrg7d59n t11;
                memset(&t11, 0, sizeof t11);
                ((uint8_t *)&t11)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t11 + 8), t10);
                F.ret = t11;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t9) + 4));
        }
    }
    {
        {
            void * t14 = gm_l0;
            gm_t9qkh0rsooc8d t15;
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)61ULL));
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_slgf61fmeeof0));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(27));
            gm_t0u0j6voh3loe t17 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t14, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t18 = t17;
            if (((((uint8_t *)&t18))[0] != 0)) {
                gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 4));
                gm_tdmdfjrg7d59n t20;
                memset(&t20, 0, sizeof t20);
                ((uint8_t *)&t20)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t20 + 8), t19);
                F.ret = t20;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t18) + 4));
        }
    }
    {
        void * t22 = gm_l0;
        gm_thk954m1ol1ig t23;
        gm_f_parse_params_3g43t5f((void *)&t23, (void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_thk954m1ol1ig t24 = t23;
        if (((((uint8_t *)&t24))[0] != 0)) {
            gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 8));
            gm_tdmdfjrg7d59n t26;
            memset(&t26, 0, sizeof t26);
            ((uint8_t *)&t26)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t26 + 8), t25);
            F.ret = t26;
            goto gm_fret;
        }
        gm_th2lqbo77pvsc t27 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t24) + 8));
        gm_l3 = t27;
    }
    {
        gm_l4 = ((void *)0);
    }
    {
        void * t29 = gm_l0;
        gm_t9qkh0rsooc8d t30;
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)60ULL));
        uint8_t t31 = gm_f_eat_qmock9g(t29, t30);
        if (!t31) goto ie28;
        {
            void * t33 = gm_l0;
            gm_to1cssm8dfiv5 t34;
            gm_f_parse_type_2m93dtq((void *)&t34, (void *)NULL, cx, NULL, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t35 = t34;
            if (((((uint8_t *)&t35))[0] != 0)) {
                gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 8));
                gm_tdmdfjrg7d59n t37;
                memset(&t37, 0, sizeof t37);
                ((uint8_t *)&t37)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t37 + 8), t36);
                F.ret = t37;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t38 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t35) + 8));
            void * t39 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t39;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t40 = gm_l0;
        uint8_t t41 = gm_l1;
        gm_thk954m1ol1ig t42;
        gm_f_parse_needs_opt_8lp1u45((void *)&t42, (void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_thk954m1ol1ig t43 = t42;
        if (((((uint8_t *)&t43))[0] != 0)) {
            gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 8));
            gm_tdmdfjrg7d59n t45;
            memset(&t45, 0, sizeof t45);
            ((uint8_t *)&t45)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t45 + 8), t44);
            F.ret = t45;
            goto gm_fret;
        }
        gm_th2lqbo77pvsc t46 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t43) + 8));
        gm_l5 = t46;
    }
    {
        gm_tlivgedjs5ps3 t47;
        memset((uint8_t *)&t47 + 17, 0, 7);
        memset((uint8_t *)&t47 + 68, 0, 4);
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t47 + 0, t48);
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 16));
        GM_ST(uint8_t, (uint8_t *)&t47 + 16, t49);
        void * t50 = gm_l4;
        GM_ST(void *, (uint8_t *)&t47 + 24, t50);
        memcpy((uint8_t *)&t47 + 32, ((uint8_t *)&gm_l5), 24);
        void * t51 = gm_l0;
        uint32_t t52 = gm_l2;
        gm_tl874t2atnlh3 t53 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t51, t52));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t47 + 56, t53);
        gm_tdmdfjrg7d59n t54;
        GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
        ((uint8_t *)&t54)[0] = 0;
        GM_ST(gm_tlivgedjs5ps3, ((uint8_t *)&t54 + 8), t47);
        gm_tdmdfjrg7d59n t55 = t54;
        uint8_t t56 = ((((uint8_t *)&t55))[0] != 0);
        F.ret = t54;
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
inline __attribute__((aligned(4))) void gm_f_parse_fn_type_only_0ifghqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_fn_type_only_0ifghqh F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK(&F, 777u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_tdmdfjrg7d59n t5;
        gm_f_parse_fn_type_sig_jrv5v82((void *)&t5, (void *)NULL, cx, NULL, t4, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tdmdfjrg7d59n t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 8));
            gm_to1cssm8dfiv5 t8;
            memset(&t8, 0, sizeof t8);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), t7);
            F.ret = t8;
            goto gm_fret;
        }
        gm_tlivgedjs5ps3 t9 = GM_LD(gm_tlivgedjs5ps3, (((uint8_t *)&t6) + 8));
        F.l2 = t9;
    }
    {
        void * t10 = gm_l0;
        gm_tq7s98ihonkju t11 = gm_f_new_48hbqcm((void *)NULL, cx, NULL, &(*(gm_tlivgedjs5ps3 *)((uint8_t *)&F.l2)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kh832np8pgs)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t12 = t11;
        if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 8, gm_srcp8o48ld4n4jq, 198, 30);
        void * t13 = GM_LD(void *, (((uint8_t *)&t12) + 8));
        gm_t2gtbh39ptekd t14;
        memset(&t14, 0, sizeof t14);
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)30ULL));
        GM_ST(void *, ((uint8_t *)&t14 + 8), t13);
        void * t15 = gm_l0;
        uint32_t t16 = gm_l1;
        gm_tl874t2atnlh3 t17 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t15, t16));
        gm_tc3oht4ksth9d t18;
        gm_f_mk_expr_53qj7nd((void *)&t18, t10, &t14, GM_CONV(gm_cseahvg8v7p0vs, t17));
        gm_to1cssm8dfiv5 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
        ((uint8_t *)&t19)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t19 + 8), t18);
        gm_to1cssm8dfiv5 t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
        F.ret = t19;
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
inline __attribute__((aligned(4))) void gm_f_parse_fn_type_sig_jrv5v82(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_parse_fn_type_sig_jrv5v82 F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* is_foreign */
    GM_STACK_CHECK(&F, 579u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t3 = gm_l1;
        gm_tdmdfjrg7d59n t4;
        gm_f_parse_fn_sig_fecr4sl((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tdmdfjrg7d59n t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 8));
            gm_tdmdfjrg7d59n t7;
            memset(&t7, 0, sizeof t7);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_tlivgedjs5ps3 t8 = GM_LD(gm_tlivgedjs5ps3, (((uint8_t *)&t5) + 8));
        F.l2 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t11 = gm_f_fn_sig_to_type_3r7l7ih((void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tlivgedjs5ps3 t12 = F.l2;
        gm_tdmdfjrg7d59n t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
        ((uint8_t *)&t13)[0] = 0;
        GM_ST(gm_tlivgedjs5ps3, ((uint8_t *)&t13 + 8), t12);
        gm_tdmdfjrg7d59n t14 = t13;
        uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
        F.ret = t13;
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
__attribute__((aligned(4))) void gm_f_parse_needs_opt_8lp1u45(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_parse_needs_opt_8lp1u45 F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* is_foreign */
    gm_tl874t2atnlh3 gm_l2; /* at */
    GM_STACK_CHECK(&F, 1128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_shv4avtqvfr4p));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
        uint8_t t5 = gm_f_at_word_18dnh5f(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            gm_th2lqbo77pvsc t8;
            memset(&t8, 0, sizeof t8);
            gm_thk954m1ol1ig t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            ((uint8_t *)&t9)[0] = 0;
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t9 + 8), t8);
            gm_thk954m1ol1ig t10 = t9;
            uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t13 = gm_l0;
            gm_tm4pc0emhrv1g t14;
            gm_f_bump_q2i40re((void *)&t14, t13);
        }
    }
    {
        void * t16 = gm_l0;
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)18ULL));
        uint8_t t18 = gm_f_at_kw_p0r3sr9(t16, t17);
        if (!t18) goto ie15;
        {
            void * t20 = gm_l0;
            gm_tl874t2atnlh3 t21 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t20));
            gm_l2 = t21;
        }
        {
            {
                void * t23 = gm_l0;
                gm_tm4pc0emhrv1g t24;
                gm_f_bump_q2i40re((void *)&t24, t23);
            }
        }
        {
            uint8_t t26 = gm_l1;
            uint8_t t27 = (uint8_t)!t26;
            if (!t27) goto ie25;
            {
                void * t29 = gm_l0;
                gm_tl874t2atnlh3 t30 = gm_l2;
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(gm_se7kda46rm021));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(106));
                gm_b0_1 t32 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t29, t30, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_thk954m1ol1ig t33;
                GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t33 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t33 + 24, 0);
                ((uint8_t *)&t33)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t33 + 8), t32);
                gm_thk954m1ol1ig t34 = t33;
                uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
                F.ret = t33;
                goto gm_fret;
            }
            goto ix25;
ie25:;
ix25:;
        }
        {
            void * t37 = gm_l0;
            gm_t3fcbuehh3bs2 t38;
            gm_f_kind_k239kpg((void *)&t38, t37);
            gm_t3fcbuehh3bs2 t39 = t38;
            uint8_t t40;
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
            if (t43 != ((uint8_t)0ULL)) goto mn42;
            t40 = ((uint8_t)1);
            goto me41;
mn42:;
            t40 = ((uint8_t)0);
            goto me41;
mn44:;
            gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 329, 12);
me41:;
            if (!t40) goto ie36;
            {
                void * t46 = gm_l0;
                void * t47 = gm_l0;
                gm_tl874t2atnlh3 t48 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t47));
                gm_b16_8 t49;
                GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s9tr38mqso4n7));
                GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(92));
                gm_b0_1 t50 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t46, t48, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_thk954m1ol1ig t51;
                GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t51 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t51 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t51 + 24, 0);
                ((uint8_t *)&t51)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t51 + 8), t50);
                gm_thk954m1ol1ig t52 = t51;
                uint8_t t53 = ((((uint8_t *)&t52))[0] != 0);
                F.ret = t51;
                goto gm_fret;
            }
            goto ix36;
ie36:;
ix36:;
        }
        {
            gm_b16_8 t54 = gm_f_empty_slice_g3tddrb();
            gm_th2lqbo77pvsc t55;
            GM_ST(uint8_t, (uint8_t *)&t55 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t55 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t55 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t55), t54);
            ((uint8_t *)&t55)[16] = 1;
            gm_thk954m1ol1ig t56;
            GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
            ((uint8_t *)&t56)[0] = 0;
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t56 + 8), t55);
            gm_thk954m1ol1ig t57 = t56;
            uint8_t t58 = ((((uint8_t *)&t57))[0] != 0);
            F.ret = t56;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        gm_f_list_vp1djv1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb59;
            {
                void * t62 = (void *)((uint8_t *)&F.l3);
                void * t63 = gm_l0;
                gm_toh0ootodhjue t64;
                GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)1));
                GM_ST(uint8_t, ((uint8_t *)&t64 + 1), ((uint8_t)0));
                GM_ST(uint8_t, ((uint8_t *)&t64 + 2), ((uint8_t)0));
                gm_to1cssm8dfiv5 t65;
                gm_f_parse_type_in_bvi7uav((void *)&t65, (void *)NULL, cx, NULL, t63, t64);
                if (gm_unw.kind) {
                    goto gm_ll60;
                }
                gm_to1cssm8dfiv5 t66 = t65;
                if (((((uint8_t *)&t66))[0] != 0)) {
                    gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 8));
                    gm_thk954m1ol1ig t68;
                    GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t68 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t68 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t68 + 24, 0);
                    ((uint8_t *)&t68)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t68 + 8), t67);
                    F.ret = t68;
                    goto gm_fret;
                }
                gm_tc3oht4ksth9d t69 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t66) + 8));
                gm_tftb1odc739t5 t70 = gm_f_push_d943vgp(t62, &t69);
                gm_tftb1odc739t5 t71 = t70;
                if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srcp8o48ld4n4jq, 336, 9);
                gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
            }
            {
                void * t74 = gm_l0;
                gm_t9qkh0rsooc8d t75;
                GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)0ULL));
                uint8_t t76 = gm_f_eat_qmock9g(t74, t75);
                uint8_t t77 = (uint8_t)!t76;
                if (!t77) goto ie73;
                {
                    goto lb59;
                }
                goto ix73;
ie73:;
ix73:;
            }
            {
                void * t79 = gm_l0;
                gm_f_skip_newlines_0ap7sq2(t79);
            }
lc59:;
        }
gm_ll60:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb59; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc59; }
        }
        goto gm_lad;
lb59:;
    }
    {
        gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_th2lqbo77pvsc t81;
        GM_ST(uint8_t, (uint8_t *)&t81 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t81 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t81 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t81), t80);
        ((uint8_t *)&t81)[16] = 1;
        gm_thk954m1ol1ig t82;
        GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
        ((uint8_t *)&t82)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t82 + 8), t81);
        gm_thk954m1ol1ig t83 = t82;
        uint8_t t84 = ((((uint8_t *)&t83))[0] != 0);
        F.ret = t82;
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
__attribute__((aligned(4))) void gm_f_parse_param_group_ac8kpvk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_param_group_ac8kpvk F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    uint64_t gm_l2; /* k */
    uint64_t gm_l3; /* names_found */
    uint8_t gm_l4; /* named */
    void * gm_l5; /* ty */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint8_t gm_l10; /* comptime */
    uint8_t gm_l11; /* rest */
    void * gm_l12; /* ty */
    void * gm_l13; /* default */
    GM_STACK_CHECK(&F, 2787u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            void * t5 = gm_l0;
            uint64_t t6 = gm_l2;
            uint8_t t7 = gm_f_nth_is_name_oqhoobd(t5, t6);
            if (!t7) goto lb4;
            {
                uint64_t t10 = gm_l3;
                uint64_t r11; bool o12 = __builtin_add_overflow(t10, ((uint64_t)1ULL), &r11);
                if (o12) gm_trap("integer overflow in `+`", gm_srcp8o48ld4n4jq, 288, 9);
                uint64_t t13 = r11;
                gm_l3 = t13;
            }
            {
                uint64_t t14 = gm_l2;
                uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
                if (o16) gm_trap("integer overflow in `+`", gm_srcp8o48ld4n4jq, 289, 9);
                uint64_t t17 = r15;
                gm_l2 = t17;
            }
            {
                uint8_t t19 = 0;
                void * t20 = gm_l0;
                uint64_t t21 = gm_l2;
                gm_t9qkh0rsooc8d t22;
                GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)57ULL));
                uint8_t t23 = gm_f_nth_is_3fgqmcb(t20, t21, t22);
                if (t23) {
                    void * t24 = gm_l0;
                    uint64_t t25 = gm_l2;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    if (o27) gm_trap("integer overflow in `+`", gm_srcp8o48ld4n4jq, 290, 55);
                    uint64_t t28 = r26;
                    uint8_t t29 = gm_f_nth_is_name_oqhoobd(t24, t28);
                    t19 = t29;
                } else t19 = 0;
                uint8_t t30 = (uint8_t)!t19;
                if (!t30) goto ie18;
                {
                    goto lb4;
                }
                goto ix18;
ie18:;
ix18:;
            }
            {
                uint64_t t32 = gm_l2;
                uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                if (o34) gm_trap("integer overflow in `+`", gm_srcp8o48ld4n4jq, 291, 9);
                uint64_t t35 = r33;
                gm_l2 = t35;
            }
lc4:;
        }
lb4:;
    }
    {
        uint8_t t36 = 0;
        uint64_t t37 = gm_l3;
        uint8_t t38 = (t37 > ((uint64_t)0ULL));
        if (t38) {
            void * t39 = gm_l0;
            uint64_t t40 = gm_l2;
            gm_t3fcbuehh3bs2 t41;
            gm_f_nth_c0r2nvs((void *)&t41, t39, t40);
            gm_tak1k6bk5qcen t42;
            gm_t9qkh0rsooc8d t43;
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)56ULL));
            gm_t3fcbuehh3bs2 t44;
            GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t44 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t44 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t44 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t44 + 8), t43);
            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t42 + 0, t44);
            gm_t9qkh0rsooc8d t45;
            GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)54ULL));
            gm_t3fcbuehh3bs2 t46;
            GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t46 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t46 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t46 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t46 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t46 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t46 + 8), t45);
            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t42 + 32, t46);
            gm_t9qkh0rsooc8d t47;
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)57ULL));
            gm_t3fcbuehh3bs2 t48;
            GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t48 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t48 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t48 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t48 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t48 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t48 + 8), t47);
            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t42 + 64, t48);
            gm_t9qkh0rsooc8d t49;
            GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)62ULL));
            gm_t3fcbuehh3bs2 t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t50 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t50 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t50 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t50 + 8), t49);
            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t42 + 96, t50);
            gm_t9qkh0rsooc8d t51;
            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)22ULL));
            gm_t3fcbuehh3bs2 t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t52 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t52 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t52 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t52 + 8), t51);
            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t42 + 128, t52);
            gm_tak1k6bk5qcen t53 = t42;
            uint8_t t54 = 0;
            gm_t3fcbuehh3bs2 t55 = t41;
            if (gm_eqt7ftn1841u(((uint8_t *)&t53) + 0, ((uint8_t *)&t55))) t54 = 1;
            if (gm_eqt7ftn1841u(((uint8_t *)&t53) + 32, ((uint8_t *)&t55))) t54 = 1;
            if (gm_eqt7ftn1841u(((uint8_t *)&t53) + 64, ((uint8_t *)&t55))) t54 = 1;
            if (gm_eqt7ftn1841u(((uint8_t *)&t53) + 96, ((uint8_t *)&t55))) t54 = 1;
            if (gm_eqt7ftn1841u(((uint8_t *)&t53) + 128, ((uint8_t *)&t55))) t54 = 1;
            uint8_t t56 = (uint8_t)(t54);
            t36 = t56;
        } else t36 = 0;
        gm_l4 = t36;
    }
    {
        uint8_t t58 = gm_l4;
        uint8_t t59 = (uint8_t)!t58;
        if (!t59) goto ie57;
        {
            void * t61 = gm_l0;
            gm_to1cssm8dfiv5 t62;
            gm_f_parse_type_2m93dtq((void *)&t62, (void *)NULL, cx, NULL, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t63 = t62;
            if (((((uint8_t *)&t63))[0] != 0)) {
                gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 8));
                gm_t8u9q1tmoidhk t65;
                memset(&t65, 0, sizeof t65);
                ((uint8_t *)&t65)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t65 + 8), t64);
                F.ret = t65;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t66 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t63) + 8));
            void * t67 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t67;
        }
        {
            gm_tf2orqcrphmek t68;
            memset((uint8_t *)&t68 + 18, 0, 6);
            memset((uint8_t *)&t68 + 52, 0, 4);
            gm_b16_8 t69 = gm_f_empty_slice_65q32rn();
            GM_ST(gm_b16_8, (uint8_t *)&t68 + 0, t69);
            GM_ST(uint8_t, (uint8_t *)&t68 + 16, ((uint8_t)0));
            GM_ST(uint8_t, (uint8_t *)&t68 + 17, ((uint8_t)0));
            void * t70 = gm_l5;
            GM_ST(void *, (uint8_t *)&t68 + 24, t70);
            GM_ST(void *, (uint8_t *)&t68 + 32, ((void *)0));
            void * t71 = gm_l0;
            uint32_t t72 = gm_l1;
            gm_tl874t2atnlh3 t73 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t71, t72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t68 + 40, t73);
            gm_t8u9q1tmoidhk t74;
            GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
            ((uint8_t *)&t74)[0] = 0;
            GM_ST(gm_tf2orqcrphmek, ((uint8_t *)&t74 + 8), t68);
            gm_t8u9q1tmoidhk t75 = t74;
            uint8_t t76 = ((((uint8_t *)&t75))[0] != 0);
            F.ret = t74;
            goto gm_fret;
        }
        goto ix57;
ie57:;
ix57:;
    }
    {
        gm_f_list_mho6jfm((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t78 = gm_l3;
            gm_l7 = t78;
        }
        {
            for (;;) {
                uint64_t t80 = gm_l8;
                uint64_t t81 = gm_l7;
                uint8_t t82 = (t80 < t81);
                if (!t82) goto lb79;
                {
                    uint64_t t85 = gm_l8;
                    uint64_t t86 = t85;
                    uint64_t t89 = GM_LD(uint64_t, ((uint8_t *)&t86));
                    gm_l9 = t89;
                    {
                        uint64_t t92 = gm_l9;
                        uint8_t t93 = (t92 > ((uint64_t)0ULL));
                        if (!t93) goto ie91;
                        {
                            {
                                void * t96 = gm_l0;
                                gm_tm4pc0emhrv1g t97;
                                gm_f_bump_q2i40re((void *)&t97, t96);
                            }
                        }
                        goto ix91;
ie91:;
ix91:;
                    }
                    {
                        void * t98 = (void *)((uint8_t *)&F.l6);
                        void * t99 = gm_l0;
                        gm_b16_8 t100;
                        GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sri0h4dm5d3ul));
                        GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(16));
                        gm_t4jn8fl3lmk5k t101;
                        gm_f_parse_name_figjvbe((void *)&t101, (void *)NULL, cx, NULL, t99, t100);
                        if (gm_unw.kind) {
                            goto gm_ll83;
                        }
                        gm_t4jn8fl3lmk5k t102 = t101;
                        if (((((uint8_t *)&t102))[0] != 0)) {
                            gm_b0_1 t103 = GM_LD(gm_b0_1, (((uint8_t *)&t102) + 8));
                            gm_t8u9q1tmoidhk t104;
                            memset(&t104, 0, sizeof t104);
                            ((uint8_t *)&t104)[0] = 1;
                            GM_ST(gm_b0_1, ((uint8_t *)&t104 + 8), t103);
                            F.ret = t104;
                            goto gm_fret;
                        }
                        gm_t08olq97rscn3 t105 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t102) + 8));
                        gm_tftb1odc739t5 t106 = gm_f_push_s5rtia7(t98, &t105);
                        gm_tftb1odc739t5 t107 = t106;
                        if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_srcp8o48ld4n4jq, 301, 9);
                        gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
                    }
                    goto me87;
mn88:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 299, 5);
me87:;
                }
lc79:;
                {
                    uint64_t t110 = gm_l8;
                    uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                    uint64_t t113 = r111;
                    gm_l8 = t113;
                }
            }
gm_ll83:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb79; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc79; }
            }
            goto gm_lad;
lb79:;
        }
    }
    {
        gm_l10 = ((uint8_t)0);
    }
    {
        gm_l11 = ((uint8_t)0);
    }
    {
        gm_l12 = ((void *)0);
    }
    {
        uint8_t t115 = 0;
        void * t116 = gm_l0;
        gm_t9qkh0rsooc8d t117;
        GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)56ULL));
        uint8_t t118 = gm_f_at_54906rm(t116, t117);
        if (!t118) {
            void * t119 = gm_l0;
            gm_t9qkh0rsooc8d t120;
            GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)54ULL));
            uint8_t t121 = gm_f_at_54906rm(t119, t120);
            t115 = t121;
        } else t115 = 1;
        if (!t115) goto ie114;
        {
            void * t123 = gm_l0;
            gm_t9qkh0rsooc8d t124;
            GM_ST(uint8_t, ((uint8_t *)&t124 + 0), ((uint8_t)54ULL));
            uint8_t t125 = gm_f_at_54906rm(t123, t124);
            gm_l10 = t125;
        }
        {
            {
                void * t127 = gm_l0;
                gm_tm4pc0emhrv1g t128;
                gm_f_bump_q2i40re((void *)&t128, t127);
            }
        }
        {
            uint8_t t130 = gm_l10;
            uint8_t t131 = (uint8_t)!t130;
            if (!t131) goto ie129;
            void * t132 = gm_l0;
            gm_t9qkh0rsooc8d t133;
            GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)50ULL));
            uint8_t t134 = gm_f_eat_qmock9g(t132, t133);
            if (!t134) goto ie129;
            {
                gm_l11 = ((uint8_t)1);
            }
            goto ix129;
ie129:;
ix129:;
        }
        {
            void * t136 = gm_l0;
            gm_to1cssm8dfiv5 t137;
            gm_f_parse_type_2m93dtq((void *)&t137, (void *)NULL, cx, NULL, t136);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t138 = t137;
            if (((((uint8_t *)&t138))[0] != 0)) {
                gm_b0_1 t139 = GM_LD(gm_b0_1, (((uint8_t *)&t138) + 8));
                gm_t8u9q1tmoidhk t140;
                memset(&t140, 0, sizeof t140);
                ((uint8_t *)&t140)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t140 + 8), t139);
                F.ret = t140;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t141 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t138) + 8));
            void * t142 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t141);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t142;
        }
        goto ix114;
ie114:;
ix114:;
    }
    {
        gm_l13 = ((void *)0);
    }
    {
        void * t144 = gm_l0;
        gm_t9qkh0rsooc8d t145;
        GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)22ULL));
        uint8_t t146 = gm_f_eat_qmock9g(t144, t145);
        if (!t146) goto ie143;
        {
            void * t148 = gm_l0;
            gm_to1cssm8dfiv5 t149;
            gm_f_parse_expr_bkb2n6e((void *)&t149, (void *)NULL, cx, NULL, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t150 = t149;
            if (((((uint8_t *)&t150))[0] != 0)) {
                gm_b0_1 t151 = GM_LD(gm_b0_1, (((uint8_t *)&t150) + 8));
                gm_t8u9q1tmoidhk t152;
                memset(&t152, 0, sizeof t152);
                ((uint8_t *)&t152)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t152 + 8), t151);
                F.ret = t152;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t153 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t150) + 8));
            void * t154 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t153);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t154;
        }
        goto ix143;
ie143:;
ix143:;
    }
    {
        gm_tf2orqcrphmek t155;
        memset((uint8_t *)&t155 + 18, 0, 6);
        memset((uint8_t *)&t155 + 52, 0, 4);
        gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t155 + 0, t156);
        uint8_t t157 = gm_l10;
        GM_ST(uint8_t, (uint8_t *)&t155 + 16, t157);
        uint8_t t158 = gm_l11;
        GM_ST(uint8_t, (uint8_t *)&t155 + 17, t158);
        void * t159 = gm_l12;
        GM_ST(void *, (uint8_t *)&t155 + 24, t159);
        void * t160 = gm_l13;
        GM_ST(void *, (uint8_t *)&t155 + 32, t160);
        void * t161 = gm_l0;
        uint32_t t162 = gm_l1;
        gm_tl874t2atnlh3 t163 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t161, t162));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t155 + 40, t163);
        gm_t8u9q1tmoidhk t164;
        GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t164 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t164 + 4, 0);
        ((uint8_t *)&t164)[0] = 0;
        GM_ST(gm_tf2orqcrphmek, ((uint8_t *)&t164 + 8), t155);
        gm_t8u9q1tmoidhk t165 = t164;
        uint8_t t166 = ((((uint8_t *)&t165))[0] != 0);
        F.ret = t164;
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
inline __attribute__((aligned(4))) void gm_f_parse_params_3g43t5f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_params_3g43t5f F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    uint8_t gm_l3; /* variadic */
    GM_STACK_CHECK(&F, 544u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_toh0ootodhjue t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t4 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t2, GM_CONV(gm_cs3cuou245tr5f, t3)));
        gm_l1 = t4;
    }
    {
    }
    {
        gm_f_list_3t77g3a((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        for (;;) {
            void * t9 = gm_l0;
            gm_t9qkh0rsooc8d t10;
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)62ULL));
            uint8_t t11 = gm_f_at_54906rm(t9, t10);
            uint8_t t12 = (uint8_t)!t11;
            if (!t12) goto lb8;
            {
                void * t16 = gm_l0;
                gm_t9qkh0rsooc8d t17;
                GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)52ULL));
                uint8_t t18 = gm_f_eat_qmock9g(t16, t17);
                if (!t18) goto ie15;
                {
                    gm_l3 = ((uint8_t)1);
                }
                {
                    {
                        void * t21 = gm_l0;
                        gm_t9qkh0rsooc8d t22;
                        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)57ULL));
                        uint8_t t23 = gm_f_eat_qmock9g(t21, t22);
                    }
                }
                {
                    goto lb8;
                }
                goto ix15;
ie15:;
ix15:;
            }
            {
                void * t24 = (void *)((uint8_t *)&F.l2);
                void * t25 = gm_l0;
                gm_t8u9q1tmoidhk t26;
                gm_f_parse_param_group_ac8kpvk((void *)&t26, (void *)NULL, cx, NULL, t25);
                if (gm_unw.kind) {
                    goto gm_ll13;
                }
                gm_t8u9q1tmoidhk t27 = t26;
                if (((((uint8_t *)&t27))[0] != 0)) {
                    gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 8));
                    gm_thk954m1ol1ig t29;
                    GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t29 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
                    ((uint8_t *)&t29)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t29 + 8), t28);
                    F.ret = t29;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ll13;
                }
                gm_tf2orqcrphmek t30 = GM_LD(gm_tf2orqcrphmek, (((uint8_t *)&t27) + 8));
                gm_tftb1odc739t5 t31 = gm_f_push_vbfukt4(t24, &t30);
                gm_tftb1odc739t5 t32 = t31;
                if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srcp8o48ld4n4jq, 274, 9);
                gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
            }
            {
                void * t35 = gm_l0;
                gm_t9qkh0rsooc8d t36;
                GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)57ULL));
                uint8_t t37 = gm_f_eat_qmock9g(t35, t36);
                uint8_t t38 = (uint8_t)!t37;
                if (!t38) goto ie34;
                {
                    goto lb8;
                }
                goto ix34;
ie34:;
ix34:;
            }
lc8:;
        }
gm_ll13:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
        }
        goto gm_ld1_1;
lb8:;
    }
    {
        {
            void * t41 = gm_l0;
            gm_t9qkh0rsooc8d t42;
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)62ULL));
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_skcto6psrjh96));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(27));
            gm_t0u0j6voh3loe t44 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t41, t42, t43);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_t0u0j6voh3loe t45 = t44;
            if (((((uint8_t *)&t45))[0] != 0)) {
                gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 4));
                gm_thk954m1ol1ig t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t47 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t47 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t47 + 24, 0);
                ((uint8_t *)&t47)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t47 + 8), t46);
                F.ret = t47;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld1_1;
            }
            gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t45) + 4));
        }
    }
    {
        gm_th2lqbo77pvsc t49;
        memset((uint8_t *)&t49 + 17, 0, 7);
        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t49 + 0, t50);
        uint8_t t51 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&t49 + 16, t51);
        gm_thk954m1ol1ig t52;
        GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
        ((uint8_t *)&t52)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t52 + 8), t49);
        gm_thk954m1ol1ig t53 = t52;
        uint8_t t54 = ((((uint8_t *)&t53))[0] != 0);
        F.ret = t52;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t54;
        goto gm_ld1_1;
    }
    {
        {
            void * t56 = gm_l0;
            gm_toh0ootodhjue t57 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t56) + 128), t57);
        }
    }
    goto gm_ls58;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t60 = gm_l0;
            gm_toh0ootodhjue t61 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t60) + 128), t61);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls58:;
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
__attribute__((aligned(4))) void gm_f_parse_prefix_type_1rn6mn8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_prefix_type_1rn6mn8 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 443u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_t3fcbuehh3bs2 t3;
        gm_f_kind_k239kpg((void *)&t3, t2);
        gm_t3fcbuehh3bs2 t4 = t3;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t17) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma15; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma15; default: goto ma15; }
ma6:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t21 != ((uint8_t)8ULL)) goto pa20;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t22 != ((uint8_t)45ULL)) goto pa20;
        goto po19;
pa20:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t24 != ((uint8_t)8ULL)) goto pa23;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t25 != ((uint8_t)47ULL)) goto pa23;
        goto po19;
pa23:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t27 != ((uint8_t)8ULL)) goto pa26;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t28 != ((uint8_t)63ULL)) goto pa26;
        goto po19;
pa26:;
        goto mn18;
po19:;
        void * t29 = gm_l0;
        gm_t9qkh0rsooc8d t30;
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)1ULL));
        gm_f_parse_type_prefix_kfrfcdd((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me5;
mn18:;
ma7:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t32 != ((uint8_t)2ULL)) goto mn31;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t33 != ((uint8_t)11ULL)) goto mn31;
        void * t34 = gm_l0;
        gm_f_parse_fn_type_only_0ifghqh((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me5;
mn31:;
ma8:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t36 != ((uint8_t)8ULL)) goto mn35;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t37 != ((uint8_t)61ULL)) goto mn35;
        void * t38 = gm_l0;
        gm_t9qkh0rsooc8d t39;
        GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)1ULL));
        gm_f_parse_paren_sq1guoi((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me5;
mn35:;
ma9:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t41 != ((uint8_t)2ULL)) goto mn40;
        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t42 != ((uint8_t)24ULL)) goto mn40;
        void * t43 = gm_l0;
        gm_f_parse_struct_lit_86pp62c((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me5;
mn40:;
ma10:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t45 != ((uint8_t)2ULL)) goto mn44;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t46 != ((uint8_t)27ULL)) goto mn44;
        void * t47 = gm_l0;
        gm_f_parse_union_lit_39ap2o5((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me5;
mn44:;
ma11:;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t49 != ((uint8_t)2ULL)) goto mn48;
        uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t50 != ((uint8_t)6ULL)) goto mn48;
        void * t51 = gm_l0;
        gm_f_parse_effect_lit_cqei081((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me5;
mn48:;
ma12:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t53 != ((uint8_t)7ULL)) goto mn52;
        void * t54 = gm_l0;
        gm_t9qkh0rsooc8d t55;
        GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)1ULL));
        gm_f_parse_attributed_00u9rjp((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me5;
mn52:;
ma13:;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t57 != ((uint8_t)6ULL)) goto mn56;
        void * t58 = gm_l0;
        gm_f_parse_intrinsic_4ou54h3((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me5;
mn56:;
ma14:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t60 != ((uint8_t)0ULL)) goto mn59;
        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_l1 = t61;
        {
            uint8_t t64 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_st2dnokm1h0jd, 4) == 0));
            if (!t64) goto ie63;
            void * t65 = gm_l0;
            gm_t9qkh0rsooc8d t66;
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)61ULL));
            uint8_t t67 = gm_f_nth_is_3fgqmcb(t65, ((uint64_t)1ULL), t66);
            if (!t67) goto ie63;
            {
                void * t69 = gm_l0;
                gm_f_parse_cont_type_ljmco4q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix63;
ie63:;
ix63:;
        }
        {
            void * t70 = gm_l0;
            gm_f_parse_type_name_d0ioo05((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me5;
mn59:;
ma15:;
        void * t72 = gm_l0;
        gm_b16_8 t73;
        GM_ST(void *, (uint8_t *)&t73, (void *)(gm_s9rm2mbmo3nku));
        GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(6));
        gm_b0_1 t74 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t72, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t75;
        memset(&t75, 0, sizeof t75);
        ((uint8_t *)&t75)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t75 + 8), t74);
        gm_to1cssm8dfiv5 t76 = t75;
        uint8_t t77 = ((((uint8_t *)&t76))[0] != 0);
        F.ret = t75;
        goto gm_fret;
        goto me5;
mn71:;
ma16:;
        gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 29, 5);
me5:;
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
inline __attribute__((aligned(4))) void gm_f_parse_type_2m93dtq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_type_2m93dtq F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 1619u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_t9qkh0rsooc8d t3 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
    }
    {
        void * t9 = gm_l0;
        gm_to1cssm8dfiv5 t10;
        gm_f_parse_prefix_type_1rn6mn8((void *)&t10, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_to1cssm8dfiv5 t11 = t10;
        if (((((uint8_t *)&t11))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 8));
            gm_to1cssm8dfiv5 t13;
            memset(&t13, 0, sizeof t13);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t13 + 8), t12);
            F.ret = t13;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_1;
        }
        gm_tc3oht4ksth9d t14 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t11) + 8));
        F.l1 = t14;
    }
    {
        void * t16 = gm_l0;
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)46ULL));
        uint8_t t18 = gm_f_eat_qmock9g(t16, t17);
        uint8_t t19 = (uint8_t)!t18;
        if (!t19) goto ie15;
        {
            gm_tc3oht4ksth9d t21 = F.l1;
            gm_to1cssm8dfiv5 t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
            ((uint8_t *)&t22)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t22 + 8), t21);
            gm_to1cssm8dfiv5 t23 = t22;
            uint8_t t24 = ((((uint8_t *)&t23))[0] != 0);
            F.ret = t22;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t24;
            goto gm_ld1_1;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        void * t25 = gm_l0;
        gm_to1cssm8dfiv5 t26;
        gm_f_parse_type_2m93dtq((void *)&t26, (void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_to1cssm8dfiv5 t27 = t26;
        if (((((uint8_t *)&t27))[0] != 0)) {
            gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 8));
            gm_to1cssm8dfiv5 t29;
            memset(&t29, 0, sizeof t29);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t29 + 8), t28);
            F.ret = t29;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_1;
        }
        gm_tc3oht4ksth9d t30 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t27) + 8));
        F.l2 = t30;
    }
    {
        void * t31 = gm_l0;
        void * t32 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        void * t33 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_t3mf58gv9kf3n t34;
        GM_ST(void *, ((uint8_t *)&t34 + 0), t32);
        GM_ST(void *, ((uint8_t *)&t34 + 8), t33);
        gm_t2gtbh39ptekd t35;
        memset(&t35, 0, sizeof t35);
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)29ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t35 + 8), t34);
        gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 72));
        gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
        gm_tl874t2atnlh3 t38 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t36), GM_CONV(gm_cseahvg8v7p0vs, t37)));
        gm_tc3oht4ksth9d t39;
        gm_f_mk_expr_53qj7nd((void *)&t39, t31, &t35, GM_CONV(gm_cseahvg8v7p0vs, t38));
        gm_to1cssm8dfiv5 t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        ((uint8_t *)&t40)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t40 + 8), t39);
        gm_to1cssm8dfiv5 t41 = t40;
        uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
        F.ret = t40;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t42;
        goto gm_ld1_1;
    }
    {
        void * t43 = gm_l0;
        gm_f_leave_rb20u32(t43);
    }
    goto gm_ls44;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t45 = gm_l0;
        gm_f_leave_rb20u32(t45);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls44:;
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
__attribute__((aligned(4))) void gm_f_parse_type_name_d0ioo05(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_type_name_d0ioo05 F;
    void * gm_l0; /* p */
    gm_t08olq97rscn3 gm_l1; /* id */
    uint32_t gm_l3; /* start */
    gm_t08olq97rscn3 gm_l4; /* name */
    gm_th2lqbo77pvsc gm_l5; /* args */
    GM_STACK_CHECK(&F, 1604u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9rm2mbmo3nku));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        gm_t4jn8fl3lmk5k t4;
        gm_f_parse_ident_n2c6b75((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4jn8fl3lmk5k t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 8));
            gm_to1cssm8dfiv5 t7;
            memset(&t7, 0, sizeof t7);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_t08olq97rscn3 t8 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t5) + 8));
        gm_l1 = t8;
    }
    {
        void * t9 = gm_l0;
        gm_t08olq97rscn3 t10 = gm_l1;
        gm_t2gtbh39ptekd t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t11 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t11 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t11 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)6ULL));
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t11 + 8), t10);
        gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l1) + 16));
        gm_f_mk_expr_53qj7nd((void *)&F.l2, t9, &t11, GM_CONV(gm_cseahvg8v7p0vs, t12));
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb13;
            {
                uint32_t t16 = GM_LD(uint32_t, ((((uint8_t *)&F.l2) + 72) + 4));
                gm_l3 = t16;
            }
            {
                void * t18 = gm_l0;
                gm_t9qkh0rsooc8d t19;
                GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)58ULL));
                uint8_t t20 = gm_f_at_54906rm(t18, t19);
                if (!t20) goto ie17;
                void * t21 = gm_l0;
                gm_t3fcbuehh3bs2 t22;
                gm_f_nth_c0r2nvs((void *)&t22, t21, ((uint64_t)1ULL));
                gm_t3fcbuehh3bs2 t23 = t22;
                uint8_t t24;
                uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
                if (t27 != ((uint8_t)0ULL)) goto mn26;
                t24 = ((uint8_t)1);
                goto me25;
mn26:;
                t24 = ((uint8_t)0);
                goto me25;
mn28:;
                gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 52, 27);
me25:;
                if (!t24) goto ie17;
                {
                    {
                        void * t31 = gm_l0;
                        gm_tm4pc0emhrv1g t32;
                        gm_f_bump_q2i40re((void *)&t32, t31);
                    }
                }
                {
                    void * t33 = gm_l0;
                    gm_b16_8 t34;
                    GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s1pvh8pfrs3ir));
                    GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(6));
                    gm_t4jn8fl3lmk5k t35;
                    gm_f_parse_ident_n2c6b75((void *)&t35, (void *)NULL, cx, NULL, t33, t34);
                    if (gm_unw.kind) {
                        goto gm_ll14;
                    }
                    gm_t4jn8fl3lmk5k t36 = t35;
                    if (((((uint8_t *)&t36))[0] != 0)) {
                        gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 8));
                        gm_to1cssm8dfiv5 t38;
                        memset(&t38, 0, sizeof t38);
                        ((uint8_t *)&t38)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t38 + 8), t37);
                        F.ret = t38;
                        goto gm_fret;
                    }
                    gm_t08olq97rscn3 t39 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t36) + 8));
                    gm_l4 = t39;
                }
                {
                    void * t40 = gm_l0;
                    gm_tioie2ju3q4ul t41;
                    void * t42 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
                    if (gm_unw.kind) {
                        goto gm_ll14;
                    }
                    GM_ST(void *, (uint8_t *)&t41 + 0, t42);
                    memcpy((uint8_t *)&t41 + 8, ((uint8_t *)&gm_l4), 32);
                    gm_t2gtbh39ptekd t43;
                    GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t43 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t43 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)22ULL));
                    GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t43 + 8), t41);
                    void * t44 = gm_l0;
                    uint32_t t45 = gm_l3;
                    gm_tl874t2atnlh3 t46 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t44, t45));
                    gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), t40, &t43, GM_CONV(gm_cseahvg8v7p0vs, t46));
                }
                goto ix17;
ie17:;
                void * t48 = gm_l0;
                gm_t9qkh0rsooc8d t49;
                GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)61ULL));
                uint8_t t50 = gm_f_at_54906rm(t48, t49);
                if (!t50) goto ie47;
                {
                    void * t52 = gm_l0;
                    gm_thk954m1ol1ig t53;
                    gm_f_parse_call_args_32e205h((void *)&t53, (void *)NULL, cx, NULL, t52);
                    if (gm_unw.kind) {
                        goto gm_ll14;
                    }
                    gm_thk954m1ol1ig t54 = t53;
                    if (((((uint8_t *)&t54))[0] != 0)) {
                        gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 8));
                        gm_to1cssm8dfiv5 t56;
                        memset(&t56, 0, sizeof t56);
                        ((uint8_t *)&t56)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t56 + 8), t55);
                        F.ret = t56;
                        goto gm_fret;
                    }
                    gm_th2lqbo77pvsc t57 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t54) + 8));
                    gm_l5 = t57;
                }
                {
                    void * t58 = gm_l0;
                    gm_tlljg61a2ctgq t59;
                    memset((uint8_t *)&t59 + 25, 0, 7);
                    void * t60 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
                    if (gm_unw.kind) {
                        goto gm_ll14;
                    }
                    GM_ST(void *, (uint8_t *)&t59 + 0, t60);
                    gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t59 + 8, t61);
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 16));
                    GM_ST(uint8_t, (uint8_t *)&t59 + 24, t62);
                    gm_t2gtbh39ptekd t63;
                    GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t63 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t63 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t63 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)19ULL));
                    GM_ST(gm_tlljg61a2ctgq, ((uint8_t *)&t63 + 8), t59);
                    void * t64 = gm_l0;
                    uint32_t t65 = gm_l3;
                    gm_tl874t2atnlh3 t66 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t64, t65));
                    gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), t58, &t63, GM_CONV(gm_cseahvg8v7p0vs, t66));
                }
                goto ix47;
ie47:;
                {
                    goto lb13;
                }
ix47:;
ix17:;
            }
lc13:;
        }
gm_ll14:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
        }
        goto gm_lad;
lb13:;
    }
    {
        gm_tc3oht4ksth9d t68 = F.l2;
        gm_to1cssm8dfiv5 t69;
        GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
        ((uint8_t *)&t69)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t69 + 8), t68);
        gm_to1cssm8dfiv5 t70 = t69;
        uint8_t t71 = ((((uint8_t *)&t70))[0] != 0);
        F.ret = t69;
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
inline __attribute__((aligned(4))) void gm_f_parse_type_operand_7guq29a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_parse_type_operand_7guq29a F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* mode */
    GM_STACK_CHECK(&F, 158u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t9qkh0rsooc8d t3 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
    }
    {
        uint8_t t10 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)1ULL)));
        if (!t10) goto ie9;
        {
            void * t12 = gm_l0;
            gm_f_parse_prefix_type_1rn6mn8((void *)&F.ret, (void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            uint8_t t13 = ((((uint8_t *)&F.ret))[0] != 0);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t13;
            goto gm_ld1_1;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t14 = gm_l0;
        gm_f_parse_postfix_ao4kstr((void *)&F.ret, (void *)NULL, cx, NULL, t14, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t15 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t15;
        goto gm_ld1_1;
    }
    {
        void * t16 = gm_l0;
        gm_f_leave_rb20u32(t16);
    }
    goto gm_ls17;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t18 = gm_l0;
        gm_f_leave_rb20u32(t18);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls17:;
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
__attribute__((aligned(4))) void gm_f_parse_type_prefix_kfrfcdd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_parse_type_prefix_kfrfcdd F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* mode */
    uint32_t gm_l2; /* start */
    uint8_t gm_l4; /* is_const */
    uint8_t gm_l6; /* is_const */
    uint8_t gm_l8; /* is_const */
    void * gm_l11; /* length */
    GM_STACK_CHECK(&F, 6190u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        gm_t9qkh0rsooc8d t6;
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)45ULL));
        uint8_t t7 = gm_f_eat_qmock9g(t5, t6);
        if (!t7) goto ie4;
        {
            void * t9 = gm_l0;
            gm_t9qkh0rsooc8d t10 = gm_l1;
            gm_to1cssm8dfiv5 t11;
            gm_f_parse_type_operand_7guq29a((void *)&t11, (void *)NULL, cx, NULL, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t12 = t11;
            if (((((uint8_t *)&t12))[0] != 0)) {
                gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 8));
                gm_to1cssm8dfiv5 t14;
                memset(&t14, 0, sizeof t14);
                ((uint8_t *)&t14)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t14 + 8), t13);
                F.ret = t14;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t15 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t12) + 8));
            F.l3 = t15;
        }
        {
            void * t16 = gm_l0;
            void * t17 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t2gtbh39ptekd t18;
            memset(&t18, 0, sizeof t18);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)24ULL));
            GM_ST(void *, ((uint8_t *)&t18 + 8), t17);
            void * t19 = gm_l0;
            uint32_t t20 = gm_l2;
            gm_tl874t2atnlh3 t21 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t19, t20));
            gm_tc3oht4ksth9d t22;
            gm_f_mk_expr_53qj7nd((void *)&t22, t16, &t18, GM_CONV(gm_cseahvg8v7p0vs, t21));
            gm_to1cssm8dfiv5 t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
            ((uint8_t *)&t23)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t23 + 8), t22);
            gm_to1cssm8dfiv5 t24 = t23;
            uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
            F.ret = t23;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t27 = gm_l0;
        gm_t9qkh0rsooc8d t28;
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)47ULL));
        uint8_t t29 = gm_f_eat_qmock9g(t27, t28);
        if (!t29) goto ie26;
        {
            void * t31 = gm_l0;
            uint8_t t32 = gm_f_eat_const_0ntbpcs(t31);
            gm_l4 = t32;
        }
        {
            void * t33 = gm_l0;
            gm_t9qkh0rsooc8d t34 = gm_l1;
            gm_to1cssm8dfiv5 t35;
            gm_f_parse_type_operand_7guq29a((void *)&t35, (void *)NULL, cx, NULL, t33, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t36 = t35;
            if (((((uint8_t *)&t36))[0] != 0)) {
                gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 8));
                gm_to1cssm8dfiv5 t38;
                memset(&t38, 0, sizeof t38);
                ((uint8_t *)&t38)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t38 + 8), t37);
                F.ret = t38;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t39 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t36) + 8));
            F.l5 = t39;
        }
        {
            void * t40 = gm_l0;
            uint8_t t41 = gm_l4;
            void * t42 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tgtub0v94d5u5 t43;
            GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), t41);
            GM_ST(void *, ((uint8_t *)&t43 + 8), t42);
            gm_t2gtbh39ptekd t44;
            memset(&t44, 0, sizeof t44);
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)25ULL));
            GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t44 + 8), t43);
            void * t45 = gm_l0;
            uint32_t t46 = gm_l2;
            gm_tl874t2atnlh3 t47 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t45, t46));
            gm_tc3oht4ksth9d t48;
            gm_f_mk_expr_53qj7nd((void *)&t48, t40, &t44, GM_CONV(gm_cseahvg8v7p0vs, t47));
            gm_to1cssm8dfiv5 t49;
            GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
            ((uint8_t *)&t49)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t49 + 8), t48);
            gm_to1cssm8dfiv5 t50 = t49;
            uint8_t t51 = ((((uint8_t *)&t50))[0] != 0);
            F.ret = t49;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t53 = gm_l0;
        gm_t9qkh0rsooc8d t54;
        GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)63ULL));
        uint8_t t55 = gm_f_eat_qmock9g(t53, t54);
        uint8_t t56 = (uint8_t)!t55;
        if (!t56) goto ie52;
        {
            void * t58 = gm_l0;
            gm_b16_8 t59;
            GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s9rm2mbmo3nku));
            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(6));
            gm_b0_1 t60 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t61;
            memset(&t61, 0, sizeof t61);
            ((uint8_t *)&t61)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t61 + 8), t60);
            gm_to1cssm8dfiv5 t62 = t61;
            uint8_t t63 = ((((uint8_t *)&t62))[0] != 0);
            F.ret = t61;
            goto gm_fret;
        }
        goto ix52;
ie52:;
ix52:;
    }
    {
        void * t65 = gm_l0;
        gm_t9qkh0rsooc8d t66;
        GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)64ULL));
        uint8_t t67 = gm_f_eat_qmock9g(t65, t66);
        if (!t67) goto ie64;
        {
            void * t69 = gm_l0;
            uint8_t t70 = gm_f_eat_const_0ntbpcs(t69);
            gm_l6 = t70;
        }
        {
            void * t71 = gm_l0;
            gm_t9qkh0rsooc8d t72 = gm_l1;
            gm_to1cssm8dfiv5 t73;
            gm_f_parse_type_operand_7guq29a((void *)&t73, (void *)NULL, cx, NULL, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t74 = t73;
            if (((((uint8_t *)&t74))[0] != 0)) {
                gm_b0_1 t75 = GM_LD(gm_b0_1, (((uint8_t *)&t74) + 8));
                gm_to1cssm8dfiv5 t76;
                memset(&t76, 0, sizeof t76);
                ((uint8_t *)&t76)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t76 + 8), t75);
                F.ret = t76;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t77 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t74) + 8));
            F.l7 = t77;
        }
        {
            void * t78 = gm_l0;
            uint8_t t79 = gm_l6;
            void * t80 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tgtub0v94d5u5 t81;
            GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t81 + 0), t79);
            GM_ST(void *, ((uint8_t *)&t81 + 8), t80);
            gm_t2gtbh39ptekd t82;
            memset(&t82, 0, sizeof t82);
            GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)26ULL));
            GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t82 + 8), t81);
            void * t83 = gm_l0;
            uint32_t t84 = gm_l2;
            gm_tl874t2atnlh3 t85 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t83, t84));
            gm_tc3oht4ksth9d t86;
            gm_f_mk_expr_53qj7nd((void *)&t86, t78, &t82, GM_CONV(gm_cseahvg8v7p0vs, t85));
            gm_to1cssm8dfiv5 t87;
            GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
            ((uint8_t *)&t87)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t87 + 8), t86);
            gm_to1cssm8dfiv5 t88 = t87;
            uint8_t t89 = ((((uint8_t *)&t88))[0] != 0);
            F.ret = t87;
            goto gm_fret;
        }
        goto ix64;
ie64:;
ix64:;
    }
    {
        void * t91 = gm_l0;
        gm_t9qkh0rsooc8d t92;
        GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)47ULL));
        uint8_t t93 = gm_f_at_54906rm(t91, t92);
        if (!t93) goto ie90;
        void * t94 = gm_l0;
        gm_t9qkh0rsooc8d t95;
        GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)64ULL));
        uint8_t t96 = gm_f_nth_is_3fgqmcb(t94, ((uint64_t)1ULL), t95);
        if (!t96) goto ie90;
        {
            {
                void * t99 = gm_l0;
                gm_tm4pc0emhrv1g t100;
                gm_f_bump_q2i40re((void *)&t100, t99);
            }
        }
        {
            {
                void * t102 = gm_l0;
                gm_tm4pc0emhrv1g t103;
                gm_f_bump_q2i40re((void *)&t103, t102);
            }
        }
        {
            void * t104 = gm_l0;
            uint8_t t105 = gm_f_eat_const_0ntbpcs(t104);
            gm_l8 = t105;
        }
        {
            void * t106 = gm_l0;
            gm_t9qkh0rsooc8d t107 = gm_l1;
            gm_to1cssm8dfiv5 t108;
            gm_f_parse_type_operand_7guq29a((void *)&t108, (void *)NULL, cx, NULL, t106, t107);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t109 = t108;
            if (((((uint8_t *)&t109))[0] != 0)) {
                gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 8));
                gm_to1cssm8dfiv5 t111;
                memset(&t111, 0, sizeof t111);
                ((uint8_t *)&t111)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t111 + 8), t110);
                F.ret = t111;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t112 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t109) + 8));
            F.l9 = t112;
        }
        {
            void * t113 = gm_l0;
            uint8_t t114 = gm_l8;
            void * t115 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l9)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tgtub0v94d5u5 t116;
            GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t116 + 0), t114);
            GM_ST(void *, ((uint8_t *)&t116 + 8), t115);
            gm_t2gtbh39ptekd t117;
            memset(&t117, 0, sizeof t117);
            GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)27ULL));
            GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t117 + 8), t116);
            void * t118 = gm_l0;
            uint32_t t119 = gm_l2;
            gm_tl874t2atnlh3 t120 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t118, t119));
            gm_tc3oht4ksth9d t121;
            gm_f_mk_expr_53qj7nd((void *)&t121, t113, &t117, GM_CONV(gm_cseahvg8v7p0vs, t120));
            gm_to1cssm8dfiv5 t122;
            GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
            ((uint8_t *)&t122)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t122 + 8), t121);
            gm_to1cssm8dfiv5 t123 = t122;
            uint8_t t124 = ((((uint8_t *)&t123))[0] != 0);
            F.ret = t122;
            goto gm_fret;
        }
        goto ix90;
ie90:;
ix90:;
    }
    {
        void * t126 = gm_l0;
        gm_t3fcbuehh3bs2 t127;
        gm_f_kind_k239kpg((void *)&t127, t126);
        gm_t3fcbuehh3bs2 t128 = t127;
        uint8_t t129 = ((GM_LD(uint8_t, ((uint8_t *)&t128) + 0) == ((uint8_t)1ULL)));
        if (!t129) goto ie125;
        void * t130 = gm_l0;
        gm_t9qkh0rsooc8d t131;
        GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)64ULL));
        uint8_t t132 = gm_f_nth_is_3fgqmcb(t130, ((uint64_t)1ULL), t131);
        if (!t132) goto ie125;
        {
            {
                void * t135 = gm_l0;
                gm_tm4pc0emhrv1g t136;
                gm_f_bump_q2i40re((void *)&t136, t135);
            }
        }
        {
            {
                void * t138 = gm_l0;
                gm_tm4pc0emhrv1g t139;
                gm_f_bump_q2i40re((void *)&t139, t138);
            }
        }
        {
            void * t140 = gm_l0;
            gm_t9qkh0rsooc8d t141 = gm_l1;
            gm_to1cssm8dfiv5 t142;
            gm_f_parse_type_operand_7guq29a((void *)&t142, (void *)NULL, cx, NULL, t140, t141);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t143 = t142;
            if (((((uint8_t *)&t143))[0] != 0)) {
                gm_b0_1 t144 = GM_LD(gm_b0_1, (((uint8_t *)&t143) + 8));
                gm_to1cssm8dfiv5 t145;
                memset(&t145, 0, sizeof t145);
                ((uint8_t *)&t145)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t145 + 8), t144);
                F.ret = t145;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t146 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t143) + 8));
            F.l10 = t146;
        }
        {
            void * t147 = gm_l0;
            void * t148 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l10)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t3mf58gv9kf3n t149;
            GM_ST(void *, ((uint8_t *)&t149 + 0), ((void *)0));
            GM_ST(void *, ((uint8_t *)&t149 + 8), t148);
            gm_t2gtbh39ptekd t150;
            memset(&t150, 0, sizeof t150);
            GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)28ULL));
            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t150 + 8), t149);
            void * t151 = gm_l0;
            uint32_t t152 = gm_l2;
            gm_tl874t2atnlh3 t153 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t151, t152));
            gm_tc3oht4ksth9d t154;
            gm_f_mk_expr_53qj7nd((void *)&t154, t147, &t150, GM_CONV(gm_cseahvg8v7p0vs, t153));
            gm_to1cssm8dfiv5 t155;
            GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t155 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t155 + 4, 0);
            ((uint8_t *)&t155)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t155 + 8), t154);
            gm_to1cssm8dfiv5 t156 = t155;
            uint8_t t157 = ((((uint8_t *)&t156))[0] != 0);
            F.ret = t155;
            goto gm_fret;
        }
        goto ix125;
ie125:;
ix125:;
    }
    {
        void * t158 = gm_l0;
        gm_toh0ootodhjue t159;
        GM_ST(uint8_t, ((uint8_t *)&t159 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t159 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t159 + 2), ((uint8_t)0));
        gm_to1cssm8dfiv5 t160;
        gm_f_parse_expr_in_45seo3h((void *)&t160, (void *)NULL, cx, NULL, t158, t159);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t161 = t160;
        if (((((uint8_t *)&t161))[0] != 0)) {
            gm_b0_1 t162 = GM_LD(gm_b0_1, (((uint8_t *)&t161) + 8));
            gm_to1cssm8dfiv5 t163;
            memset(&t163, 0, sizeof t163);
            ((uint8_t *)&t163)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t163 + 8), t162);
            F.ret = t163;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t164 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t161) + 8));
        void * t165 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t164);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t165;
    }
    {
        void * t167 = gm_l0;
        gm_t9qkh0rsooc8d t168;
        GM_ST(uint8_t, ((uint8_t *)&t168 + 0), ((uint8_t)57ULL));
        uint8_t t169 = gm_f_at_54906rm(t167, t168);
        if (!t169) goto ie166;
        {
            void * t171 = gm_l0;
            gm_tl874t2atnlh3 t172 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t171));
            gm_b16_8 t173;
            GM_ST(void *, (uint8_t *)&t173, (void *)(gm_sc71n5mgucblh));
            GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(49));
            gm_f_error_s6hn871((void *)&F.l12, (void *)NULL, cx, NULL, t172, t173);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t174 = (void *)((uint8_t *)&F.l12);
            gm_b16_8 t175;
            GM_ST(void *, (uint8_t *)&t175, (void *)(gm_spferj7kepn49));
            GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(114));
            gm_f_add_note_46v502s(t174, t175);
        }
        {
            void * t176 = gm_l0;
            gm_b0_1 t177 = gm_f_fail_diag_u0q4kp3(t176, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l12)));
            gm_to1cssm8dfiv5 t178;
            memset(&t178, 0, sizeof t178);
            ((uint8_t *)&t178)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t178 + 8), t177);
            gm_to1cssm8dfiv5 t179 = t178;
            uint8_t t180 = ((((uint8_t *)&t179))[0] != 0);
            F.ret = t178;
            goto gm_fret;
        }
        goto ix166;
ie166:;
ix166:;
    }
    {
        {
            void * t182 = gm_l0;
            gm_t9qkh0rsooc8d t183;
            GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)64ULL));
            gm_b16_8 t184;
            GM_ST(void *, (uint8_t *)&t184, (void *)(gm_sh7ob55fi0i62));
            GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(25));
            gm_t0u0j6voh3loe t185 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t182, t183, t184);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t186 = t185;
            if (((((uint8_t *)&t186))[0] != 0)) {
                gm_b0_1 t187 = GM_LD(gm_b0_1, (((uint8_t *)&t186) + 4));
                gm_to1cssm8dfiv5 t188;
                memset(&t188, 0, sizeof t188);
                ((uint8_t *)&t188)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t188 + 8), t187);
                F.ret = t188;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t189 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t186) + 4));
        }
    }
    {
        void * t190 = gm_l0;
        gm_t9qkh0rsooc8d t191 = gm_l1;
        gm_to1cssm8dfiv5 t192;
        gm_f_parse_type_operand_7guq29a((void *)&t192, (void *)NULL, cx, NULL, t190, t191);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t193 = t192;
        if (((((uint8_t *)&t193))[0] != 0)) {
            gm_b0_1 t194 = GM_LD(gm_b0_1, (((uint8_t *)&t193) + 8));
            gm_to1cssm8dfiv5 t195;
            memset(&t195, 0, sizeof t195);
            ((uint8_t *)&t195)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t195 + 8), t194);
            F.ret = t195;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t196 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t193) + 8));
        F.l13 = t196;
    }
    {
        void * t197 = gm_l0;
        void * t198 = gm_l11;
        void * t199 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l13)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3mf58gv9kf3n t200;
        GM_ST(void *, ((uint8_t *)&t200 + 0), t198);
        GM_ST(void *, ((uint8_t *)&t200 + 8), t199);
        gm_t2gtbh39ptekd t201;
        memset(&t201, 0, sizeof t201);
        GM_ST(uint8_t, ((uint8_t *)&t201 + 0), ((uint8_t)28ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t201 + 8), t200);
        void * t202 = gm_l0;
        uint32_t t203 = gm_l2;
        gm_tl874t2atnlh3 t204 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t202, t203));
        gm_tc3oht4ksth9d t205;
        gm_f_mk_expr_53qj7nd((void *)&t205, t197, &t201, GM_CONV(gm_cseahvg8v7p0vs, t204));
        gm_to1cssm8dfiv5 t206;
        GM_ST(uint8_t, (uint8_t *)&t206 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t206 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t206 + 4, 0);
        ((uint8_t *)&t206)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t206 + 8), t205);
        gm_to1cssm8dfiv5 t207 = t206;
        uint8_t t208 = ((((uint8_t *)&t207))[0] != 0);
        F.ret = t206;
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
__attribute__((aligned(4))) void gm_f_parse_where_opt_a5p2ja6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_where_opt_a5p2ja6 F;
    void * gm_l0; /* p */
    uint32_t gm_l2; /* start */
    gm_t08olq97rscn3 gm_l3; /* name */
    void * gm_l4; /* t */
    GM_STACK_CHECK(&F, 1142u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_5kg5cjm((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_smglbbtmdmip2));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
        uint8_t t5 = gm_f_at_word_18dnh5f(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            gm_tt7ek1a8bopcl t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            ((uint8_t *)&t9)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 8), t8);
            gm_tt7ek1a8bopcl t10 = t9;
            uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t13 = gm_l0;
            gm_tm4pc0emhrv1g t14;
            gm_f_bump_q2i40re((void *)&t14, t13);
        }
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb15;
            {
                void * t18 = gm_l0;
                uint32_t t19 = gm_f_lo_fp82gt4(t18);
                gm_l2 = t19;
            }
            {
                void * t20 = gm_l0;
                gm_b16_8 t21;
                GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sandsuergllg3));
                GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(29));
                gm_t4jn8fl3lmk5k t22;
                gm_f_parse_ident_n2c6b75((void *)&t22, (void *)NULL, cx, NULL, t20, t21);
                if (gm_unw.kind) {
                    goto gm_ll16;
                }
                gm_t4jn8fl3lmk5k t23 = t22;
                if (((((uint8_t *)&t23))[0] != 0)) {
                    gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 8));
                    gm_tt7ek1a8bopcl t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t25 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t25 + 16, 0);
                    ((uint8_t *)&t25)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t25 + 8), t24);
                    F.ret = t25;
                    goto gm_fret;
                }
                gm_t08olq97rscn3 t26 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t23) + 8));
                gm_l3 = t26;
            }
            {
                {
                    void * t28 = gm_l0;
                    gm_t9qkh0rsooc8d t29;
                    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)61ULL));
                    gm_b16_8 t30;
                    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sa8mtcad5rfmf));
                    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(25));
                    gm_t0u0j6voh3loe t31 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t28, t29, t30);
                    if (gm_unw.kind) {
                        goto gm_ll16;
                    }
                    gm_t0u0j6voh3loe t32 = t31;
                    if (((((uint8_t *)&t32))[0] != 0)) {
                        gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 4));
                        gm_tt7ek1a8bopcl t34;
                        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t34 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t34 + 16, 0);
                        ((uint8_t *)&t34)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 8), t33);
                        F.ret = t34;
                        goto gm_fret;
                    }
                    gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t32) + 4));
                }
            }
            {
                void * t36 = gm_l0;
                gm_toh0ootodhjue t37;
                GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)0));
                GM_ST(uint8_t, ((uint8_t *)&t37 + 1), ((uint8_t)0));
                GM_ST(uint8_t, ((uint8_t *)&t37 + 2), ((uint8_t)0));
                gm_to1cssm8dfiv5 t38;
                gm_f_parse_type_in_bvi7uav((void *)&t38, (void *)NULL, cx, NULL, t36, t37);
                if (gm_unw.kind) {
                    goto gm_ll16;
                }
                gm_to1cssm8dfiv5 t39 = t38;
                if (((((uint8_t *)&t39))[0] != 0)) {
                    gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 8));
                    gm_tt7ek1a8bopcl t41;
                    GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t41 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t41 + 16, 0);
                    ((uint8_t *)&t41)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t41 + 8), t40);
                    F.ret = t41;
                    goto gm_fret;
                }
                gm_tc3oht4ksth9d t42 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t39) + 8));
                void * t43 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t42);
                if (gm_unw.kind) {
                    goto gm_ll16;
                }
                gm_l4 = t43;
            }
            {
                {
                    void * t45 = gm_l0;
                    gm_t9qkh0rsooc8d t46;
                    GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)62ULL));
                    gm_b16_8 t47;
                    GM_ST(void *, (uint8_t *)&t47, (void *)(gm_s8dujv4dnroq0));
                    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(23));
                    gm_t0u0j6voh3loe t48 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t45, t46, t47);
                    if (gm_unw.kind) {
                        goto gm_ll16;
                    }
                    gm_t0u0j6voh3loe t49 = t48;
                    if (((((uint8_t *)&t49))[0] != 0)) {
                        gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 4));
                        gm_tt7ek1a8bopcl t51;
                        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t51 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t51 + 16, 0);
                        ((uint8_t *)&t51)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t51 + 8), t50);
                        F.ret = t51;
                        goto gm_fret;
                    }
                    gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t49) + 4));
                }
            }
            {
                void * t53 = (void *)((uint8_t *)&F.l1);
                gm_tpv68m88f9lcr t54;
                memset((uint8_t *)&t54 + 52, 0, 4);
                memcpy((uint8_t *)&t54 + 0, ((uint8_t *)&gm_l3), 32);
                void * t55 = gm_l4;
                GM_ST(void *, (uint8_t *)&t54 + 32, t55);
                void * t56 = gm_l0;
                uint32_t t57 = gm_l2;
                gm_tl874t2atnlh3 t58 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t56, t57));
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t54 + 40, t58);
                gm_tftb1odc739t5 t59 = gm_f_push_aqjeqnr(t53, &t54);
                gm_tftb1odc739t5 t60 = t59;
                if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_srcp8o48ld4n4jq, 354, 9);
                gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
            }
            {
                uint8_t t63 = 0;
                uint8_t t64 = 0;
                void * t65 = gm_l0;
                gm_t9qkh0rsooc8d t66;
                GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)57ULL));
                uint8_t t67 = gm_f_eat_qmock9g(t65, t66);
                if (t67) {
                    void * t68 = gm_l0;
                    gm_t3fcbuehh3bs2 t69;
                    gm_f_kind_k239kpg((void *)&t69, t68);
                    gm_t3fcbuehh3bs2 t70 = t69;
                    uint8_t t71;
                    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t70) + 0));
                    if (t74 != ((uint8_t)0ULL)) goto mn73;
                    t71 = ((uint8_t)1);
                    goto me72;
mn73:;
                    t71 = ((uint8_t)0);
                    goto me72;
mn75:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 355, 35);
me72:;
                    t64 = t71;
                } else t64 = 0;
                if (t64) {
                    void * t76 = gm_l0;
                    gm_t9qkh0rsooc8d t77;
                    GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)61ULL));
                    uint8_t t78 = gm_f_nth_is_3fgqmcb(t76, ((uint64_t)1ULL), t77);
                    t63 = t78;
                } else t63 = 0;
                uint8_t t79 = (uint8_t)!t63;
                if (!t79) goto ie62;
                {
                    goto lb15;
                }
                goto ix62;
ie62:;
ix62:;
            }
lc15:;
        }
gm_ll16:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb15; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc15; }
        }
        goto gm_lad;
lb15:;
    }
    {
        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_tt7ek1a8bopcl t82;
        GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
        ((uint8_t *)&t82)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t82 + 8), t81);
        gm_tt7ek1a8bopcl t83 = t82;
        uint8_t t84 = ((((uint8_t *)&t83))[0] != 0);
        F.ret = t82;
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
