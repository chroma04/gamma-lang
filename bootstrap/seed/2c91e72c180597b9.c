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
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_b16_8 f3; } gm_t26d2uc07m5o2;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { gm_tcc1rq4n1avb2 f0; uint8_t f1; uint8_t pe[3]; } gm_t736djc66po0l;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_b16_8 f3; uint8_t f4; uint8_t pe[7]; } gm_tg9l3k9j9m9b9;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; gm_tl874t2atnlh3 f3; } gm_tis7ttivmj935;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t f3; uint8_t pe[3]; } gm_tkrkmsnb6m1b7;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tllos8achgacq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t f3; uint8_t pe[2]; } gm_tppdoi1slt2pr;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_tl874t2atnlh3 f2; gm_b16_8 f3; } gm_tt384v6n8ojib;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_tt7dbdq4n1egc;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t26d2uc07m5o2) == 48 && _Alignof(gm_t26d2uc07m5o2) == 8 && offsetof(gm_t26d2uc07m5o2, f0) == 0 && offsetof(gm_t26d2uc07m5o2, f1) == 16 && offsetof(gm_t26d2uc07m5o2, f2) == 28 && offsetof(gm_t26d2uc07m5o2, f3) == 32, "layout of `(name: string, span: Span, vis: Visibility, what: string)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { uint64_t f0; gm_tsoie1ppgdvcg f1; } gm_t5im2v50m3hk6;
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; gm_ts6oqppjh0ba3 f2; gm_ts6oqppjh0ba3 f3; } gm_t6htphat2tk14;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t736djc66po0l) == 12 && _Alignof(gm_t736djc66po0l) == 4 && offsetof(gm_t736djc66po0l, f0) == 0 && offsetof(gm_t736djc66po0l, f1) == 8, "layout of `?(u32, u32)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; } gm_t9ldr1q5hn9fq;
typedef struct { void * f0; uint8_t f1; uint8_t p2[7]; gm_t4c7ipjj9s3fp f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_t4c7ipjj9s3fp f5; gm_trqfvmjtt4sij f6; } gm_tad8216qc9dro;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg9l3k9j9m9b9) == 72 && _Alignof(gm_tg9l3k9j9m9b9) == 8 && offsetof(gm_tg9l3k9j9m9b9, f0) == 0 && offsetof(gm_tg9l3k9j9m9b9, f1) == 16 && offsetof(gm_tg9l3k9j9m9b9, f2) == 32 && offsetof(gm_tg9l3k9j9m9b9, f3) == 48 && offsetof(gm_tg9l3k9j9m9b9, f4) == 64, "layout of `Dangling`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tis7ttivmj935) == 56 && _Alignof(gm_tis7ttivmj935) == 8 && offsetof(gm_tis7ttivmj935, f0) == 0 && offsetof(gm_tis7ttivmj935, f1) == 16 && offsetof(gm_tis7ttivmj935, f2) == 32 && offsetof(gm_tis7ttivmj935, f3) == 44, "layout of `(span: Span, name: string, local: Span, origin: Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tppdoi1slt2pr v0; gm_t08olq97rscn3 v1; gm_tt384v6n8ojib v2; gm_tt7dbdq4n1egc v3; gm_toceffb697p0u v4; gm_tl874t2atnlh3 v5; gm_t26d2uc07m5o2 v6; gm_tllos8achgacq v7; gm_tkrkmsnb6m1b7 v8; gm_tis7ttivmj935 v9; gm_tis7ttivmj935 v10; } f1; } gm_tj5f8vajf35o7;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkrkmsnb6m1b7) == 48 && _Alignof(gm_tkrkmsnb6m1b7) == 8 && offsetof(gm_tkrkmsnb6m1b7, f0) == 0 && offsetof(gm_tkrkmsnb6m1b7, f1) == 16 && offsetof(gm_tkrkmsnb6m1b7, f2) == 32 && offsetof(gm_tkrkmsnb6m1b7, f3) == 44, "layout of `(span: Span, name: string, catch_all: Span, is_result: bool)`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tllos8achgacq) == 64 && _Alignof(gm_tllos8achgacq) == 8 && offsetof(gm_tllos8achgacq, f0) == 0 && offsetof(gm_tllos8achgacq, f1) == 16 && offsetof(gm_tllos8achgacq, f2) == 32 && offsetof(gm_tllos8achgacq, f3) == 48, "layout of `(span: Span, name: string, local: Span, why: string)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tppdoi1slt2pr) == 32 && _Alignof(gm_tppdoi1slt2pr) == 8 && offsetof(gm_tppdoi1slt2pr, f0) == 0 && offsetof(gm_tppdoi1slt2pr, f1) == 16 && offsetof(gm_tppdoi1slt2pr, f2) == 28 && offsetof(gm_tppdoi1slt2pr, f3) == 29, "layout of `(name: string, span: Span, param: bool, assigned: bool)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
_Static_assert(sizeof(gm_tt384v6n8ojib) == 56 && _Alignof(gm_tt384v6n8ojib) == 8 && offsetof(gm_tt384v6n8ojib, f0) == 0 && offsetof(gm_tt384v6n8ojib, f1) == 16 && offsetof(gm_tt384v6n8ojib, f2) == 28 && offsetof(gm_tt384v6n8ojib, f3) == 40, "layout of `(name: string, span: Span, item: Span, what: string)`");
_Static_assert(sizeof(gm_tt7dbdq4n1egc) == 28 && _Alignof(gm_tt7dbdq4n1egc) == 4 && offsetof(gm_tt7dbdq4n1egc, f0) == 0 && offsetof(gm_tt7dbdq4n1egc, f1) == 12 && offsetof(gm_tt7dbdq4n1egc, f2) == 24, "layout of `(span: Span, operand: Span, same: bool)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t5im2v50m3hk6) == 48 && _Alignof(gm_t5im2v50m3hk6) == 8 && offsetof(gm_t5im2v50m3hk6, f0) == 0 && offsetof(gm_t5im2v50m3hk6, f1) == 8, "layout of `(i: usize, x: LocalDecl)`");
_Static_assert(sizeof(gm_t6htphat2tk14) == 8 && _Alignof(gm_t6htphat2tk14) == 1 && offsetof(gm_t6htphat2tk14, f0) == 0 && offsetof(gm_t6htphat2tk14, f1) == 2 && offsetof(gm_t6htphat2tk14, f2) == 4 && offsetof(gm_t6htphat2tk14, f3) == 6, "layout of `(BinOp, BinOp, BinOp, BinOp)`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9ldr1q5hn9fq) == 4 && _Alignof(gm_t9ldr1q5hn9fq) == 1 && offsetof(gm_t9ldr1q5hn9fq, f0) == 0 && offsetof(gm_t9ldr1q5hn9fq, f1) == 2, "layout of `(BinOp, BinOp)`");
_Static_assert(sizeof(gm_tad8216qc9dro) == 248 && _Alignof(gm_tad8216qc9dro) == 8 && offsetof(gm_tad8216qc9dro, f0) == 0 && offsetof(gm_tad8216qc9dro, f1) == 8 && offsetof(gm_tad8216qc9dro, f2) == 16 && offsetof(gm_tad8216qc9dro, f3) == 72 && offsetof(gm_tad8216qc9dro, f4) == 112 && offsetof(gm_tad8216qc9dro, f5) == 152 && offsetof(gm_tad8216qc9dro, f6) == 208, "layout of `Checked`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tj5f8vajf35o7) == 72 && _Alignof(gm_tj5f8vajf35o7) == 8 && offsetof(gm_tj5f8vajf35o7, f0) == 0 && offsetof(gm_tj5f8vajf35o7, f1) == 8, "layout of `Finding`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
struct gm_fr_analyze_np1m50h {
    gm_frame h;
    gm_tad8216qc9dro l4; /* c */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_assigned_53ipnmg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_binds_busqeo0 {
    gm_frame h;
    void * l0; /* c */
    uint32_t l1; /* f */
    gm_b0_1 ret;
};
struct gm_fr_block_stmts_c2nnk6m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bool_lit_q44074e {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_contains_14ai471 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_behvl9q {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_gk4cjgd {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_gm6v230 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_otc7v7a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_file_ok_4j31vl0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_259eng6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_o40bhnh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_generic_of_1861hm9 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_get_ptr_1a4voen {
    gm_frame h;
    void * ret;
};
struct gm_fr_hash_map_t9h7o8p {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_tvpcibu {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_is_array_p7ehais {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_decl_used_oldf2ja {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_used_outside_tbk81ks {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_1aqn740 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fsi5en1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mutate_oi1s4vs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_needless_pub_jqsnq13 {
    gm_frame h;
    gm_tkacitepg5fk0 l11; /* a */
    gm_tv2uf4leil5hq l12; /* b */
    gm_tagrkbqh054h7 l13; /* state */
    gm_t6qooom8qa2do l14; /* v */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_path_eq_l3qbb7v {
    gm_frame h;
    gm_tbci5ospk5nnn l20; /* x */
    gm_tbci5ospk5nnn l21; /* y */
    uint8_t ret;
};
struct gm_fr_root_oq23mha {
    gm_frame h;
    gm_t736djc66po0l ret;
};
struct gm_fr_run_checks_c17ks6r {
    gm_frame h;
    gm_b16_8 l1; /* scoped */
    gm_trqfvmjtt4sij l4; /* fn_decl */
    gm_t6qooom8qa2do l10; /* v */
    gm_trqfvmjtt4sij l17; /* calls */
    gm_trqfvmjtt4sij l29; /* calls */
    gm_trqfvmjtt4sij l37; /* growth */
    gm_trqfvmjtt4sij l42; /* dangling */
    gm_tg9l3k9j9m9b9 l45; /* d */
    gm_tsoie1ppgdvcg l51; /* l */
    gm_tv2uf4leil5hq l65; /* b */
    gm_tkacitepg5fk0 l69; /* a */
    gm_tagrkbqh054h7 l70; /* state */
    gm_t6qooom8qa2do l73; /* v */
    gm_trqfvmjtt4sij l74; /* kept */
    gm_tj5f8vajf35o7 l77; /* f */
    gm_b0_1 ret;
};
struct gm_fr_same_value_7pehjnc {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_use_of_q9agaul {
    gm_frame h;
    void * ret;
};
struct gm_fr_visit_unu9ms6 {
    gm_frame h;
    void * l0; /* c */
    uint32_t l1; /* f */
    void * l3; /* calls */
    gm_tsth9qu1dv0g3 l9; /* callee */
    gm_t710lsrq1m8gk l23; /* x */
    gm_t710lsrq1m8gk l40; /* x */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b);
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92_2(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_analyze_np1m50h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_assigned_53ipnmg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_binds_busqeo0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_stmts_c2nnk6m(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_bool_lit_q44074e(void * a0);
__attribute__((aligned(4))) void * gm_f_const_value_fr4coi4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_14ai471(void * a0, gm_tcc1rq4n1avb2 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_behvl9q(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gk4cjgd(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gm6v230(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_otc7v7a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_file_ok_4j31vl0(void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_c753csd(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_efqiqfc(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_259eng6(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o40bhnh(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_frame_up_h0ddp1d(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_get_ptr_1a4voen(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_q3uemcr(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_rlnefnq(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_growth_summaries_dguakce(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_t9h7o8p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_tvpcibu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_is_array_p7ehais(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_decl_used_oldf2ja(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_used_outside_tbk81ks(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_1aqn740(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fsi5en1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mutate_oi1s4vs(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mutated_copies_vc6lpss(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_needless_pub_jqsnq13(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_path_eq_l3qbb7v(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_possible_dangling_addres_d403vr0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_present_only_bindings_ug2i6dq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rqlbv1e(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s4q134v(void * a0, gm_tf5srh4mp4b3i a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3g1tdpr(void * a0, gm_tcc1rq4n1avb2 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_he2q4rq(void * a0, gm_tcc1rq4n1avb2 a1, const void * a2);
__attribute__((aligned(4))) gm_t736djc66po0l gm_f_root_oq23mha(void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_run_checks_c17ks6r(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_same_value_7pehjnc(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_stale_element_pointers_jjgn0ll(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void * gm_f_use_of_q9agaul(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_visit_unu9ms6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3);
extern uint8_t gm_c9b4lh7gft4[4] __attribute__((aligned(4)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s1ft2nrvnhcbp[10];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_sa0amdcmvej1l[14];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sg3kpa3h16no3[7];
extern const uint8_t gm_skr6p9lfc5ftp[11];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_spe8t3e79vnal[7];
extern const uint8_t gm_sptnm8c6c52dr[7];
extern const uint8_t gm_sq0dqiq83b924[9];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srco9vbah234t6e[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_svklio0qebpog[9];
extern const uint8_t gm_svqdrhms9i231[7];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b) {
    if (a[8] != b[8]) return false;
    return !a[8] || gm_eq6groq37ram(a, b);
}
inline bool gm_eqbqb4gb1d92_2(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)1) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)2) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)3) return gm_eqehhdtpeti1(a + 1, b + 1);
    return true;
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_bool_lit_q44074e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bool_lit_q44074e F;
    void * gm_l0; /* x */
    uint8_t gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 30u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)4ULL)) goto ie2;
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 8));
        gm_l1 = t5;
        {
            uint8_t t7 = gm_l1;
            gm_tqs5p4n2tq19v t8;
            GM_ST(uint8_t, ((uint8_t *)&t8), t7);
            ((uint8_t *)&t8)[1] = 1;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_tqs5p4n2tq19v t10;
        memset(&t10, 0, sizeof t10);
        uint8_t t11 = 0;
        F.ret = t10;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_14ai471(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_14ai471 F;
    void * gm_l0; /* m */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    GM_STACK_CHECK(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_efqiqfc(t2, t3);
        uint8_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) goto mn7;
        t5 = ((uint8_t)0);
        goto me6;
mn7:;
        t5 = ((uint8_t)1);
        goto me6;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 153, 12);
me6:;
        uint8_t t9 = 0;
        F.ret = t5;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_behvl9q(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_behvl9q F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 72)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gk4cjgd(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_gk4cjgd F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gm6v230(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_gm6v230 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_otc7v7a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_otc7v7a F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 12)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        int32_t t9 = gm_f_mag_cmp_s39veq6(t6, t8);
        uint8_t t10 = ((t9 == ((int32_t)0LL)));
        t1 = t10;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_file_ok_4j31vl0(void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0) {
    struct gm_fr_file_ok_4j31vl0 F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    gm_tl874t2atnlh3 t3 = gm_l0;
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
    uint8_t t5 = (!gm_eqff85ac10o3(((uint8_t *)&t3), ((uint8_t *)&t4)));
    if (t5) {
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 0));
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_run_checks_c17ks6r *)((&F.h)->up))->l1) + 8);
        uint32_t t8 = (uint32_t)t7;
        uint8_t t9 = (t6 < t8);
        t2 = t9;
    } else t2 = 0;
    if (t2) {
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 0));
        uint64_t t11 = (uint64_t)t10;
        uint64_t t12 = (uintptr_t)t11;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_run_checks_c17ks6r *)((&F.h)->up))->l1) + 8);
        if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srco9vbah234t6e, 99, 116);
        uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&((struct gm_fr_run_checks_c17ks6r *)((&F.h)->up))->l1)) + t12 * 1));
        t1 = t14;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_259eng6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_259eng6 F;
    void * gm_l0; /* child */
    GM_STACK_CHECK(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_visit_unu9ms6 *)((&F.h)->up))->l0));
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_visit_unu9ms6 *)((&F.h)->up))->l1));
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_visit_unu9ms6 *)((&F.h)->up))->l3));
        gm_b0_1 t6 = gm_f_visit_unu9ms6((void *)NULL, cx, NULL, t2, t3, t4, t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o40bhnh(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_o40bhnh F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_binds_busqeo0 *)((&F.h)->up))->l0));
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_binds_busqeo0 *)((&F.h)->up))->l1));
        void * t4 = gm_l0;
        gm_b0_1 t5 = gm_f_binds_busqeo0((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_generic_of_1861hm9 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tiilmfd16v3fc gm_l2; /* inst */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t3 = gm_l1;
        uint64_t t4 = (uint64_t)t3;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
        if (t5 >= (uintptr_t)(t7)) gm_trap_index(t5, t7, gm_srclv0dcbbfhves, 590, 16);
        if (!(((((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t5 * 136) + 96))[24] != 0)) goto ie2;
        gm_tiilmfd16v3fc t8 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t5 * 136) + 96));
        gm_l2 = t8;
        {
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t12 = gm_l1;
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_get_ptr_1a4voen(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ptr_1a4voen F;
    void * gm_l0; /* m */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_c753csd(t2, t3);
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
        void * t12 = (void *)(((uint8_t *)GM_LD(void *, (((uint8_t *)t10) + 0)) + t9 * 64) + 16);
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_t9h7o8p(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_t9h7o8p F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_gm6v230();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_tvpcibu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_tvpcibu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_gk4cjgd();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_array_p7ehais(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_array_p7ehais F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    gm_f_kind_jcnt4hm((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
    uint8_t t5;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t10 != ((uint8_t)11ULL)) goto pa9;
    goto po8;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t12 != ((uint8_t)20ULL)) goto pa11;
    goto po8;
pa11:;
    goto mn7;
po8:;
    t5 = ((uint8_t)1);
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn13:;
    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 265, 61);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_decl_used_oldf2ja(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_decl_used_oldf2ja F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 2544) + 0) + 8);
    uint8_t t5 = (t2 < t4);
    if (t5) {
        uint64_t t6 = gm_l1;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 2544) + 0) + 8);
        if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_src6lm8esmv16p1, 61, 91);
        uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t8) + 2544) + 0)) + t7 * 1));
        t1 = t10;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_8d4f9ek F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t9 != ((uint8_t)0ULL)) goto pa8;
    goto po7;
pa8:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t11 != ((uint8_t)1ULL)) goto pa10;
    goto po7;
pa10:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t13 != ((uint8_t)2ULL)) goto pa12;
    goto po7;
pa12:;
    goto mn6;
po7:;
    t4 = ((uint8_t)1);
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_used_outside_tbk81ks(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_used_outside_tbk81ks F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 2584) + 0) + 8);
    uint8_t t5 = (t2 < t4);
    if (t5) {
        uint64_t t6 = gm_l1;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 2584) + 0) + 8);
        if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_src6lm8esmv16p1, 64, 97);
        uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t8) + 2584) + 0)) + t7 * 1));
        t1 = t10;
    } else t1 = 0;
    F.ret = t1;
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
inline __attribute__((aligned(4))) void gm_f_list_1aqn740(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1aqn740 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_behvl9q();
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
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7s4om10 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_r81t436();
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
inline __attribute__((aligned(4))) void gm_f_list_fsi5en1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fsi5en1 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_otc7v7a();
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
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_go2qg0b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_d914i5u();
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mutate_oi1s4vs(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_mutate_oi1s4vs F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* place */
    gm_tcc1rq4n1avb2 gm_l3; /* k */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint32_t t4 = gm_l1;
        void * t5 = gm_l2;
        gm_t736djc66po0l t6 = gm_f_root_oq23mha(t3, t4, t5);
        if (!((((uint8_t *)&t6))[8] != 0)) goto ie2;
        gm_tcc1rq4n1avb2 t7 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            void * t9 = gm_l0;
            gm_tcc1rq4n1avb2 t10 = gm_l3;
            void * t11 = gm_f_use_of_q9agaul((void *)NULL, cx, NULL, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (((uint8_t *)t11) + 40), ((uint8_t)1));
        }
        goto ix2;
ie2:;
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
__attribute__((aligned(4))) void gm_f_analyze_np1m50h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2) {
    struct gm_fr_analyze_np1m50h F;
    void * gm_l0; /* s */
    uint8_t gm_l1; /* program */
    gm_b16_8 gm_l2; /* scoped */
    uint64_t gm_l3; /* cn */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 410u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l3;
            void * t4 = gm_l0;
            uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 1968) + 0) + 8);
            uint8_t t6 = (t3 < t5);
            if (!t6) goto lb2;
            {
                uint64_t t10 = gm_l3;
                uint64_t t11 = (uintptr_t)t10;
                void * t12 = gm_l0;
                uint64_t t13 = GM_LD(uintptr_t, ((((uint8_t *)t12) + 1968) + 0) + 8);
                if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srco9vbah234t6e, 77, 12);
                uint8_t t14 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t12) + 1968) + 0)) + t11 * 88) + 72) + 0) == ((uint8_t)0ULL)));
                if (!t14) goto ie9;
                {
                    {
                        void * t17 = gm_l0;
                        uint64_t t18 = gm_l3;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l0;
                        uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 1968) + 0) + 8);
                        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srco9vbah234t6e, 78, 38);
                        gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 1968) + 0)) + t19 * 88) + 16));
                        uint64_t t23 = gm_l3;
                        uint64_t t24 = (uintptr_t)t23;
                        void * t25 = gm_l0;
                        uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 1968) + 0) + 8);
                        if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_srco9vbah234t6e, 78, 63);
                        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t25) + 1968) + 0)) + t24 * 88) + 28));
                        void * t28 = gm_f_const_value_fr4coi4((void *)NULL, cx, NULL, t17, t22, t27);
                        if (gm_unw.kind) {
                            goto gm_ll7;
                        }
                    }
                }
                goto ix9;
ie9:;
ix9:;
            }
            {
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `+`", gm_srco9vbah234t6e, 80, 9);
                uint64_t t32 = r30;
                gm_l3 = t32;
            }
lc2:;
        }
gm_ll7:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        memset((uint8_t *)&F.l4 + 9, 0, 7);
        void * t33 = gm_l0;
        GM_ST(void *, (uint8_t *)&F.l4 + 0, t33);
        uint8_t t34 = gm_l1;
        GM_ST(uint8_t, (uint8_t *)&F.l4 + 8, t34);
        gm_f_hash_map_tvpcibu((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l4 + 16)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_s83jn2q((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l4 + 72)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_s83jn2q((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l4 + 112)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_t9h7o8p((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l4 + 152)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_1aqn740((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l4 + 208)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            void * t36 = gm_l0;
            void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
            uint64_t t38 = GM_LD(uintptr_t, ((((uint8_t *)t37) + 240) + 0) + 8);
            gm_l5 = t38;
        }
        {
            for (;;) {
                uint64_t t40 = gm_l6;
                uint64_t t41 = gm_l5;
                uint8_t t42 = (t40 < t41);
                if (!t42) goto lb39;
                {
                    uint64_t t45 = gm_l6;
                    {
                        void * t49 = (void *)(((uint8_t *)&F.l4) + 72);
                        gm_tftb1odc739t5 t50 = gm_f_push_vd1290p(t49, ((uint8_t)0));
                        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_srco9vbah234t6e, 91, 39);
                        gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
                    }
                    goto me46;
mn47:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 91, 5);
me46:;
                }
lc39:;
                {
                    uint64_t t53 = gm_l6;
                    uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                    uint64_t t56 = r54;
                    gm_l6 = t56;
                }
            }
lb39:;
        }
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            void * t58 = gm_l0;
            uint64_t t59 = GM_LD(uintptr_t, ((((uint8_t *)t58) + 344) + 0) + 8);
            gm_l7 = t59;
        }
        {
            for (;;) {
                uint64_t t61 = gm_l8;
                uint64_t t62 = gm_l7;
                uint8_t t63 = (t61 < t62);
                if (!t63) goto lb60;
                {
                    uint64_t t66 = gm_l8;
                    {
                        void * t70 = (void *)(((uint8_t *)&F.l4) + 112);
                        gm_tftb1odc739t5 t71 = gm_f_push_vd1290p(t70, ((uint8_t)0));
                        if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srco9vbah234t6e, 92, 35);
                        gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                    }
                    goto me67;
mn68:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 92, 5);
me67:;
                }
lc60:;
                {
                    uint64_t t74 = gm_l8;
                    uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                    uint64_t t77 = r75;
                    gm_l8 = t77;
                }
            }
lb60:;
        }
    }
    {
        void * t78 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t79 = gm_l2;
        gm_b0_1 t80 = gm_f_run_checks_c17ks6r((void *)NULL, cx, NULL, t78, t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(gm_out, (((uint8_t *)&F.l4) + 208), 40);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_assigned_53ipnmg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_assigned_53ipnmg F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* place */
    gm_tcc1rq4n1avb2 gm_l3; /* k */
    void * gm_l4; /* u */
    uint8_t gm_l5; /* seen */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tl874t2atnlh3 gm_l8; /* w */
    GM_STACK_CHECK(&F, 344u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        void * t4 = gm_l2;
        gm_b0_1 t5 = gm_f_mutate_oi1s4vs((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t7 = 0;
        void * t8 = gm_l2;
        uint8_t t9;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t12 != ((uint8_t)15ULL)) goto mn11;
        t9 = ((uint8_t)1);
        goto me10;
mn11:;
        t9 = ((uint8_t)0);
        goto me10;
mn13:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 378, 8);
me10:;
        if (!t9) {
            void * t14 = gm_l2;
            uint8_t t15;
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            if (t18 != ((uint8_t)16ULL)) goto mn17;
            t15 = ((uint8_t)1);
            goto me16;
mn17:;
            t15 = ((uint8_t)0);
            goto me16;
mn19:;
            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 378, 35);
me16:;
            t7 = t15;
        } else t7 = 1;
        if (!t7) goto ie6;
        {
            void * t22 = gm_l0;
            uint32_t t23 = gm_l1;
            void * t24 = gm_l2;
            gm_t736djc66po0l t25 = gm_f_root_oq23mha(t22, t23, t24);
            if (!((((uint8_t *)&t25))[8] != 0)) goto ie21;
            gm_tcc1rq4n1avb2 t26 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)&t25));
            gm_l3 = t26;
            {
                void * t28 = gm_l0;
                gm_tcc1rq4n1avb2 t29 = gm_l3;
                void * t30 = gm_f_use_of_q9agaul((void *)NULL, cx, NULL, t28, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t30;
            }
            {
                gm_l5 = ((uint8_t)0);
            }
            {
                {
                    void * t32 = gm_l4;
                    gm_b16_8 t33 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 0) + 0));
                    gm_l6 = t33;
                }
                {
                    gm_l7 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t35 = gm_l7;
                        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        uint8_t t37 = (t35 < t36);
                        if (!t37) goto lb34;
                        {
                            uint64_t t40 = gm_l7;
                            uint64_t t41 = (uintptr_t)t40;
                            uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            gm_tl874t2atnlh3 t45 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t41 * 12));
                            gm_l8 = t45;
                            {
                                gm_tl874t2atnlh3 t48 = gm_l8;
                                void * t49 = gm_l2;
                                gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t49) + 72));
                                uint8_t t51 = (gm_eqff85ac10o3(((uint8_t *)&t48), ((uint8_t *)&t50)));
                                if (!t51) goto ie47;
                                {
                                    gm_l5 = ((uint8_t)1);
                                }
                                goto ix47;
ie47:;
ix47:;
                            }
                            goto me43;
mn44:;
                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 382, 13);
me43:;
                        }
lc34:;
                        {
                            uint64_t t54 = gm_l7;
                            uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                            uint64_t t57 = r55;
                            gm_l7 = t57;
                        }
                    }
lb34:;
                }
            }
            {
                uint8_t t59 = gm_l5;
                uint8_t t60 = (uint8_t)!t59;
                if (!t60) goto ie58;
                {
                    void * t62 = gm_l4;
                    void * t63 = (void *)(((uint8_t *)t62) + 0);
                    void * t64 = gm_l2;
                    gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 72));
                    gm_tftb1odc739t5 t66 = gm_f_push_0mk7udh(t63, GM_CONV(gm_cseahvg8v7p0vs, t65));
                    if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srco9vbah234t6e, 385, 27);
                    gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
                }
                goto ix58;
ie58:;
ix58:;
            }
            goto ix21;
ie21:;
ix21:;
        }
        goto ix6;
ie6:;
ix6:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_binds_busqeo0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_binds_busqeo0 F;
    void * gm_l2; /* p */
    uint32_t gm_l3; /* l */
    GM_STACK_CHECK(&F, 91u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t5 != ((uint8_t)1ULL)) goto mn4;
        uint32_t t6 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t6;
        void * t7 = F.l0;
        void * t8 = (void *)(((uint8_t *)t7) + 152);
        uint32_t t9 = F.l1;
        uint32_t t10 = gm_l3;
        gm_tcc1rq4n1avb2 t11;
        GM_ST(uint32_t, ((uint8_t *)&t11 + 0), t9);
        GM_ST(uint32_t, ((uint8_t *)&t11 + 4), t10);
        gm_tftb1odc739t5 t12 = gm_f_put_3g1tdpr(t8, t11, ((uint8_t)1));
        if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srco9vbah234t6e, 345, 21);
        gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
        goto me3;
mn4:;
        void * t15 = gm_l2;
        gm_closure t16 = ((gm_closure){(void *)gm_f_fn_o40bhnh, (void *)&F.h, cx});
        gm_f_for_each_subpat_arlrtrm(t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn14:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 344, 5);
me3:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_block_stmts_c2nnk6m(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_block_stmts_c2nnk6m F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* stmts */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_trcmmd12cefb1 gm_l5; /* st */
    uint32_t gm_l6; /* local */
    gm_b16_8 gm_l7; /* locals */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t9805qtonbsag gm_l10; /* l */
    uint32_t gm_l11; /* id */
    void * gm_l12; /* place */
    void * gm_l13; /* value */
    gm_b16_8 gm_l14; /* places */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    void * gm_l17; /* p */
    void * gm_l18; /* x */
    GM_STACK_CHECK(&F, 905u);
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
                    gm_trcmmd12cefb1 t15 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 32));
                    gm_l5 = t15;
                    {
                        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        switch (t24) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma20; case ((uint8_t)3ULL): goto ma21; case ((uint8_t)4ULL): goto ma22; case ((uint8_t)5ULL): goto ma22; default: goto ma22; }
ma18:;
                        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t26 != ((uint8_t)0ULL)) goto mn25;
                        uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l6 = t27;
                        void * t28 = gm_l0;
                        void * t29 = (void *)(((uint8_t *)t28) + 152);
                        uint32_t t30 = gm_l1;
                        uint32_t t31 = gm_l6;
                        gm_tcc1rq4n1avb2 t32;
                        GM_ST(uint32_t, ((uint8_t *)&t32 + 0), t30);
                        GM_ST(uint32_t, ((uint8_t *)&t32 + 4), t31);
                        gm_tftb1odc739t5 t33 = gm_f_put_3g1tdpr(t29, t32, ((uint8_t)1));
                        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srco9vbah234t6e, 354, 31);
                        gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
                        goto me17;
mn25:;
ma19:;
                        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t36 != ((uint8_t)1ULL)) goto mn35;
                        gm_b16_8 t37 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l7 = t37;
                        {
                            {
                                gm_b16_8 t40 = gm_l7;
                                gm_l8 = t40;
                            }
                            {
                                gm_l9 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t42 = gm_l9;
                                    uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                    uint8_t t44 = (t42 < t43);
                                    if (!t44) goto lb41;
                                    {
                                        uint64_t t47 = gm_l9;
                                        uint64_t t48 = (uintptr_t)t47;
                                        uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                        gm_t9805qtonbsag t52 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t48 * 8));
                                        gm_l10 = t52;
                                        {
                                            if (!((((uint8_t *)&gm_l10))[4] != 0)) goto ie54;
                                            uint32_t t55 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                                            gm_l11 = t55;
                                            {
                                                void * t57 = gm_l0;
                                                void * t58 = (void *)(((uint8_t *)t57) + 152);
                                                uint32_t t59 = gm_l1;
                                                uint32_t t60 = gm_l11;
                                                gm_tcc1rq4n1avb2 t61;
                                                GM_ST(uint32_t, ((uint8_t *)&t61 + 0), t59);
                                                GM_ST(uint32_t, ((uint8_t *)&t61 + 4), t60);
                                                gm_tftb1odc739t5 t62 = gm_f_put_3g1tdpr(t58, t61, ((uint8_t)1));
                                                if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srco9vbah234t6e, 357, 34);
                                                gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
                                            }
                                            goto ix54;
ie54:;
ix54:;
                                        }
                                        goto me50;
mn51:;
                                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 356, 17);
me50:;
                                    }
lc41:;
                                    {
                                        uint64_t t65 = gm_l9;
                                        uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                                        uint64_t t68 = r66;
                                        gm_l9 = t68;
                                    }
                                }
lb41:;
                            }
                        }
                        goto me17;
mn35:;
ma20:;
                        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t70 != ((uint8_t)2ULL)) goto mn69;
                        void * t71 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l12 = t71;
                        void * t72 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                        gm_l13 = t72;
                        {
                            void * t74 = gm_l0;
                            uint32_t t75 = gm_l1;
                            void * t76 = gm_l12;
                            gm_b0_1 t77 = gm_f_assigned_53ipnmg((void *)NULL, cx, NULL, t74, t75, t76);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t79 = gm_l0;
                            void * t80 = gm_l12;
                            void * t81 = gm_l13;
                            uint8_t t82 = gm_f_same_value_7pehjnc(t79, t80, t81);
                            if (!t82) goto ie78;
                            void * t83 = gm_l12;
                            gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t83) + 72));
                            gm_tl874t2atnlh3 t85 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                            uint8_t t86 = (!gm_eqff85ac10o3(((uint8_t *)&t84), ((uint8_t *)&t85)));
                            if (!t86) goto ie78;
                            void * t87 = gm_l13;
                            gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + 72));
                            gm_tl874t2atnlh3 t89 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                            uint8_t t90 = (!gm_eqff85ac10o3(((uint8_t *)&t88), ((uint8_t *)&t89)));
                            if (!t90) goto ie78;
                            {
                                void * t92 = gm_l0;
                                void * t93 = (void *)(((uint8_t *)t92) + 208);
                                void * t94 = gm_l12;
                                gm_tl874t2atnlh3 t95 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t94) + 72));
                                void * t96 = gm_l13;
                                gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t96) + 72));
                                gm_tl874t2atnlh3 t98 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t95), GM_CONV(gm_cseahvg8v7p0vs, t97)));
                                gm_tj5f8vajf35o7 t99;
                                memset(&t99, 0, sizeof t99);
                                GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)5ULL));
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t99 + 8), t98);
                                gm_tftb1odc739t5 t100 = gm_f_push_rqlbv1e(t93, &t99);
                                if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_srco9vbah234t6e, 363, 21);
                                gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
                            }
                            goto ix78;
ie78:;
ix78:;
                        }
                        goto me17;
mn69:;
ma21:;
                        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t103 != ((uint8_t)3ULL)) goto mn102;
                        gm_b16_8 t104 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l14 = t104;
                        {
                            {
                                gm_b16_8 t107 = gm_l14;
                                gm_l15 = t107;
                            }
                            {
                                gm_l16 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t109 = gm_l16;
                                    uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                    uint8_t t111 = (t109 < t110);
                                    if (!t111) goto lb108;
                                    {
                                        uint64_t t114 = gm_l16;
                                        uint64_t t115 = (uintptr_t)t114;
                                        uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                        void * t117 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t115 * 8));
                                        void * t120 = GM_LD(void *, ((uint8_t *)&t117));
                                        gm_l17 = t120;
                                        {
                                            void * t123 = gm_l17;
                                            if (!(GM_LD(void *, ((uint8_t *)&t123)) != 0)) goto ie122;
                                            void * t124 = GM_LD(void *, ((uint8_t *)&t123));
                                            gm_l18 = t124;
                                            {
                                                void * t126 = gm_l0;
                                                uint32_t t127 = gm_l1;
                                                void * t128 = gm_l18;
                                                gm_b0_1 t129 = gm_f_assigned_53ipnmg((void *)NULL, cx, NULL, t126, t127, t128);
                                                if (gm_unw.kind) {
                                                    goto gm_ll112;
                                                }
                                            }
                                            goto ix122;
ie122:;
ix122:;
                                        }
                                        goto me118;
mn119:;
                                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 367, 17);
me118:;
                                    }
lc108:;
                                    {
                                        uint64_t t131 = gm_l16;
                                        uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
                                        uint64_t t134 = r132;
                                        gm_l16 = t134;
                                    }
                                }
gm_ll112:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb108; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc108; }
                                }
                                goto gm_ll8;
lb108:;
                            }
                        }
                        goto me17;
mn102:;
ma22:;
                        goto me17;
mn135:;
ma23:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 353, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 352, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t138 = gm_l4;
                    uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                    uint64_t t141 = r139;
                    gm_l4 = t141;
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
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_bool_lit_q44074e(void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_file_ok_4j31vl0(void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_259eng6(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o40bhnh(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_array_p7ehais(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_mutate_oi1s4vs(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_needless_pub_jqsnq13(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_needless_pub_jqsnq13 F;
    void * gm_l0; /* c */
    uint64_t gm_l1; /* d */
    gm_b16_8 gm_l2; /* scoped */
    void * gm_l3; /* s */
    void * gm_l4; /* decl */
    gm_b16_8 gm_l5; /* name */
    uint8_t gm_l6; /* candidate */
    gm_tl874t2atnlh3 gm_l7; /* span */
    uint8_t gm_l8; /* file_ok */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    GM_STACK_CHECK(&F, 1087u);
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
        uint64_t t4 = gm_l1;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l3;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 240) + 0) + 8);
        if (t5 >= (uintptr_t)(t8)) gm_trap_index(t5, t8, gm_srco9vbah234t6e, 223, 14);
        void * t9 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 240) + 0)) + t5 * 216);
        gm_l4 = t9;
    }
    {
        void * t10 = gm_l4;
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((((uint8_t *)t10) + 0) + 0));
        gm_l5 = t11;
    }
    {
        void * t12 = gm_l4;
        uint8_t t13;
        uint8_t t19 = GM_LD(uint8_t, (((((uint8_t *)t12) + 40) + 56) + 0));
        switch (t19) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma17; default: goto ma18; }
ma15:;
        uint8_t t21 = GM_LD(uint8_t, (((((uint8_t *)t12) + 40) + 56) + 0));
        if (t21 != ((uint8_t)0ULL)) goto mn20;
        t13 = ((uint8_t)0);
        goto me14;
mn20:;
ma16:;
        uint8_t t23 = GM_LD(uint8_t, (((((uint8_t *)t12) + 40) + 56) + 0));
        if (t23 != ((uint8_t)1ULL)) goto mn22;
        t13 = ((uint8_t)1);
        goto me14;
mn22:;
ma17:;
        uint8_t t25 = GM_LD(uint8_t, (((((uint8_t *)t12) + 40) + 56) + 0));
        if (t25 != ((uint8_t)2ULL)) goto mn24;
        void * t26 = gm_l0;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)t26) + 8));
        t13 = t27;
        goto me14;
mn24:;
ma18:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 225, 18);
me14:;
        gm_l6 = t13;
    }
    {
        void * t28 = gm_l4;
        gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t28) + 0) + 16));
        gm_l7 = t29;
    }
    {
        uint8_t t30 = 0;
        uint8_t t31 = 0;
        gm_tl874t2atnlh3 t32 = gm_l7;
        gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t34 = (!gm_eqff85ac10o3(((uint8_t *)&t32), ((uint8_t *)&t33)));
        if (t34) {
            uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 0));
            uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint32_t t37 = (uint32_t)t36;
            uint8_t t38 = (t35 < t37);
            t31 = t38;
        } else t31 = 0;
        if (t31) {
            uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 0));
            uint64_t t40 = (uint64_t)t39;
            uint64_t t41 = (uintptr_t)t40;
            uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srco9vbah234t6e, 231, 83);
            uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t41 * 1));
            t30 = t43;
        } else t30 = 0;
        gm_l8 = t30;
    }
    {
        uint8_t t45 = 0;
        uint8_t t46 = 0;
        uint8_t t47 = 0;
        uint8_t t48 = 0;
        uint8_t t49 = gm_l6;
        uint8_t t50 = (uint8_t)!t49;
        if (!t50) {
            uint8_t t51 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l5)), gm_sog4rksrmsqd2, 4) == 0));
            t48 = t51;
        } else t48 = 1;
        if (!t48) {
            void * t52 = gm_l4;
            uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)t52) + 32));
            uint64_t t54 = (uintptr_t)t53;
            void * t55 = gm_l3;
            void * t56 = GM_LD(void *, (((uint8_t *)t55) + 0));
            uint64_t t57 = GM_LD(uintptr_t, ((((uint8_t *)t56) + 200) + 0) + 8);
            if (t54 >= (uintptr_t)(t57)) gm_trap_index(t54, t57, gm_srco9vbah234t6e, 232, 43);
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t56) + 200) + 0)) + t54 * 184) + 112));
            t47 = t58;
        } else t47 = 1;
        if (!t47) {
            uint8_t t59 = gm_l8;
            uint8_t t60 = (uint8_t)!t59;
            t46 = t60;
        } else t46 = 1;
        if (!t46) {
            void * t61 = gm_l3;
            uint64_t t62 = gm_l1;
            uint8_t t63 = gm_f_is_used_outside_tbk81ks(t61, t62);
            t45 = t63;
        } else t45 = 1;
        if (!t45) goto ie44;
        {
            gm_th2lqbo77pvsc t65;
            memset(&t65, 0, sizeof t65);
            uint8_t t66 = 0;
            F.ret = t65;
            goto gm_fret;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        {
            void * t68 = gm_l4;
            gm_b16_8 t69 = GM_LD(gm_b16_8, ((((uint8_t *)t68) + 40) + 40));
            gm_l9 = t69;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t71 = gm_l10;
                uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t73 = (t71 < t72);
                if (!t73) goto lb70;
                {
                    uint64_t t76 = gm_l10;
                    uint64_t t77 = (uintptr_t)t76;
                    uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_tkacitepg5fk0 t81 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t77 * 72));
                    F.l11 = t81;
                    {
                        gm_b16_8 t84 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l11) + 0) + 0));
                        gm_t6j6smj7qoctu t85;
                        gm_b16_8 t86;
                        GM_ST(void *, (uint8_t *)&t86, (void *)(gm_spe8t3e79vnal));
                        GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(6));
                        GM_ST(gm_b16_8, (uint8_t *)&t85 + 0, t86);
                        gm_b16_8 t87;
                        GM_ST(void *, (uint8_t *)&t87, (void *)(gm_s1ft2nrvnhcbp));
                        GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(9));
                        GM_ST(gm_b16_8, (uint8_t *)&t85 + 16, t87);
                        uint8_t t88 = 0;
                        if (gm_eq0iqiubfm7e(((uint8_t *)&t85) + 0, ((uint8_t *)&t84))) t88 = 1;
                        if (gm_eq0iqiubfm7e(((uint8_t *)&t85) + 16, ((uint8_t *)&t84))) t88 = 1;
                        uint8_t t89 = (uint8_t)(t88);
                        if (!t89) goto ie83;
                        {
                            gm_th2lqbo77pvsc t91;
                            memset(&t91, 0, sizeof t91);
                            uint8_t t92 = 0;
                            F.ret = t91;
                            goto gm_fret;
                        }
                        goto ix83;
ie83:;
ix83:;
                    }
                    goto me79;
mn80:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 235, 5);
me79:;
                }
lc70:;
                {
                    uint64_t t94 = gm_l10;
                    uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                    uint64_t t97 = r95;
                    gm_l10 = t97;
                }
            }
lb70:;
        }
    }
    {
        void * t99 = gm_l4;
        uint8_t t100 = GM_LD(uint8_t, (((((uint8_t *)t99) + 40) + 64) + 0));
        if (t100 != ((uint8_t)0ULL)) goto ie98;
        gm_tv2uf4leil5hq t101 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t99) + 40) + 64) + 8));
        F.l12 = t101;
        {
            void * t103 = gm_l3;
            uint64_t t104 = gm_l1;
            gm_f_state_a5bnsjb((void *)&F.l13, (void *)NULL, cx, NULL, t103, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
            if (t106 != ((uint8_t)2ULL)) goto ie105;
            gm_t6qooom8qa2do t107 = GM_LD(gm_t6qooom8qa2do, (((uint8_t *)&F.l13) + 8));
            F.l14 = t107;
            {
                void * t110 = GM_LD(void *, (((uint8_t *)&F.l12) + 40));
                uint8_t t111;
                uint8_t t114 = GM_LD(uint8_t, ((((uint8_t *)t110) + 8) + 0));
                if (t114 != ((uint8_t)36ULL)) goto mn113;
                t111 = ((uint8_t)1);
                goto me112;
mn113:;
                t111 = ((uint8_t)0);
                goto me112;
mn115:;
                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 241, 8);
me112:;
                if (!t111) goto ie109;
                {
                    uint8_t t118;
                    uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                    if (t121 != ((uint8_t)5ULL)) goto mn120;
                    t118 = ((uint8_t)1);
                    goto me119;
mn120:;
                    t118 = ((uint8_t)0);
                    goto me119;
mn122:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 242, 12);
me119:;
                    if (!t118) goto ie117;
                    {
                        gm_b16_8 t124;
                        GM_ST(void *, (uint8_t *)&t124, (void *)(gm_sq0dqiq83b924));
                        GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t124);
                        goto gm_fdone;
                    }
                    goto ix117;
ie117:;
ix117:;
                }
                goto ix109;
ie109:;
ix109:;
            }
            {
                uint8_t t126 = 0;
                uint8_t t127;
                uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                if (t130 != ((uint8_t)7ULL)) goto mn129;
                t127 = ((uint8_t)1);
                goto me128;
mn129:;
                t127 = ((uint8_t)0);
                goto me128;
mn131:;
                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 244, 8);
me128:;
                if (!t127) {
                    uint8_t t132;
                    uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                    if (t135 != ((uint8_t)8ULL)) goto mn134;
                    t132 = ((uint8_t)1);
                    goto me133;
mn134:;
                    t132 = ((uint8_t)0);
                    goto me133;
mn136:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 244, 26);
me133:;
                    t126 = t132;
                } else t126 = 1;
                if (!t126) goto ie125;
                {
                    gm_b16_8 t138;
                    GM_ST(void *, (uint8_t *)&t138, (void *)(gm_svklio0qebpog));
                    GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t138);
                    goto gm_fdone;
                }
                goto ix125;
ie125:;
ix125:;
            }
            {
                gm_th2lqbo77pvsc t139;
                memset(&t139, 0, sizeof t139);
                uint8_t t140 = 0;
                F.ret = t139;
                goto gm_fret;
            }
            goto ix105;
ie105:;
            {
                gm_th2lqbo77pvsc t142;
                memset(&t142, 0, sizeof t142);
                uint8_t t143 = 0;
                F.ret = t142;
                goto gm_fret;
            }
ix105:;
        }
        goto ix98;
ie98:;
        {
            gm_th2lqbo77pvsc t145;
            memset(&t145, 0, sizeof t145);
            uint8_t t146 = 0;
            F.ret = t145;
            goto gm_fret;
        }
ix98:;
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
__attribute__((aligned(4))) uint8_t gm_f_path_eq_l3qbb7v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_path_eq_l3qbb7v F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    uint32_t gm_l2; /* x */
    uint32_t gm_l3; /* y */
    uint32_t gm_l4; /* l1 */
    uint32_t gm_l5; /* x */
    uint32_t gm_l6; /* l2 */
    uint32_t gm_l7; /* y */
    uint64_t gm_l8; /* x */
    uint64_t gm_l9; /* y */
    void * gm_l10; /* b1 */
    uint32_t gm_l11; /* x1 */
    void * gm_l12; /* b2 */
    uint32_t gm_l13; /* x2 */
    void * gm_l14; /* x */
    void * gm_l15; /* y */
    void * gm_l16; /* b1 */
    void * gm_l17; /* x1 */
    void * gm_l18; /* b2 */
    void * gm_l19; /* x2 */
    GM_STACK_CHECK(&F, 565u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma10; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma10; case ((uint8_t)31ULL): goto ma10; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma10; case ((uint8_t)34ULL): goto ma10; case ((uint8_t)35ULL): goto ma10; case ((uint8_t)36ULL): goto ma10; case ((uint8_t)37ULL): goto ma10; case ((uint8_t)38ULL): goto ma10; case ((uint8_t)39ULL): goto ma10; case ((uint8_t)40ULL): goto ma10; case ((uint8_t)41ULL): goto ma10; case ((uint8_t)42ULL): goto ma10; case ((uint8_t)43ULL): goto ma10; case ((uint8_t)44ULL): goto ma10; case ((uint8_t)45ULL): goto ma10; case ((uint8_t)46ULL): goto ma10; case ((uint8_t)47ULL): goto ma10; case ((uint8_t)48ULL): goto ma10; case ((uint8_t)49ULL): goto ma10; case ((uint8_t)50ULL): goto ma10; case ((uint8_t)51ULL): goto ma10; case ((uint8_t)52ULL): goto ma10; case ((uint8_t)53ULL): goto ma10; case ((uint8_t)54ULL): goto ma10; case ((uint8_t)55ULL): goto ma10; case ((uint8_t)56ULL): goto ma10; case ((uint8_t)57ULL): goto ma10; case ((uint8_t)58ULL): goto ma10; default: goto ma10; }
ma4:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)15ULL)) goto mn13;
        uint32_t t15 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t15;
        {
            void * t18 = gm_l1;
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            if (t19 != ((uint8_t)15ULL)) goto ie17;
            uint32_t t20 = GM_LD(uint32_t, ((((uint8_t *)t18) + 8) + 8));
            gm_l3 = t20;
            {
                uint32_t t22 = gm_l2;
                uint32_t t23 = gm_l3;
                uint8_t t24 = ((t22 == t23));
                uint8_t t25 = 0;
                F.ret = t24;
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        {
            uint8_t t26 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn13:;
ma5:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t28 != ((uint8_t)16ULL)) goto mn27;
        uint32_t t29 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t29;
        uint32_t t30 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 4));
        gm_l5 = t30;
        {
            void * t33 = gm_l1;
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
            if (t34 != ((uint8_t)16ULL)) goto ie32;
            uint32_t t35 = GM_LD(uint32_t, (((((uint8_t *)t33) + 8) + 8) + 0));
            gm_l6 = t35;
            uint32_t t36 = GM_LD(uint32_t, (((((uint8_t *)t33) + 8) + 8) + 4));
            gm_l7 = t36;
            {
                uint8_t t38 = 0;
                uint32_t t39 = gm_l4;
                uint32_t t40 = gm_l6;
                uint8_t t41 = ((t39 == t40));
                if (t41) {
                    uint32_t t42 = gm_l5;
                    uint32_t t43 = gm_l7;
                    uint8_t t44 = ((t42 == t43));
                    t38 = t44;
                } else t38 = 0;
                uint8_t t45 = 0;
                F.ret = t38;
                goto gm_fret;
            }
            goto ix32;
ie32:;
ix32:;
        }
        {
            uint8_t t46 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn27:;
ma6:;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t48 != ((uint8_t)17ULL)) goto mn47;
        uint64_t t49 = GM_LD(uint64_t, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l8 = t49;
        {
            void * t52 = gm_l1;
            uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t52) + 8) + 0));
            if (t53 != ((uint8_t)17ULL)) goto ie51;
            uint64_t t54 = GM_LD(uint64_t, (((((uint8_t *)t52) + 8) + 8) + 16));
            gm_l9 = t54;
            {
                uint64_t t56 = gm_l8;
                uint64_t t57 = gm_l9;
                uint8_t t58 = ((t56 == t57));
                uint8_t t59 = 0;
                F.ret = t58;
                goto gm_fret;
            }
            goto ix51;
ie51:;
ix51:;
        }
        {
            uint8_t t60 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn47:;
ma7:;
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t62 != ((uint8_t)19ULL)) goto mn61;
        void * t63 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l10 = t63;
        uint32_t t64 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l11 = t64;
        {
            void * t67 = gm_l1;
            uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t67) + 8) + 0));
            if (t68 != ((uint8_t)19ULL)) goto ie66;
            void * t69 = GM_LD(void *, (((((uint8_t *)t67) + 8) + 8) + 0));
            gm_l12 = t69;
            uint32_t t70 = GM_LD(uint32_t, (((((uint8_t *)t67) + 8) + 8) + 8));
            gm_l13 = t70;
            {
                uint8_t t72 = 0;
                uint32_t t73 = gm_l11;
                uint32_t t74 = gm_l13;
                uint8_t t75 = ((t73 == t74));
                if (t75) {
                    void * t76 = gm_l10;
                    void * t77 = gm_l12;
                    uint8_t t78 = gm_f_path_eq_l3qbb7v(t76, t77);
                    t72 = t78;
                } else t72 = 0;
                uint8_t t79 = 0;
                F.ret = t72;
                goto gm_fret;
            }
            goto ix66;
ie66:;
ix66:;
        }
        {
            uint8_t t80 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn61:;
ma8:;
        uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t82 != ((uint8_t)18ULL)) goto mn81;
        void * t83 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l14 = t83;
        {
            void * t86 = gm_l1;
            uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
            if (t87 != ((uint8_t)18ULL)) goto ie85;
            void * t88 = GM_LD(void *, ((((uint8_t *)t86) + 8) + 8));
            gm_l15 = t88;
            {
                void * t90 = gm_l14;
                void * t91 = gm_l15;
                uint8_t t92 = gm_f_path_eq_l3qbb7v(t90, t91);
                uint8_t t93 = 0;
                F.ret = t92;
                goto gm_fret;
            }
            goto ix85;
ie85:;
ix85:;
        }
        {
            uint8_t t94 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn81:;
ma9:;
        uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t96 != ((uint8_t)20ULL)) goto mn95;
        void * t97 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l16 = t97;
        void * t98 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l17 = t98;
        {
            void * t101 = gm_l1;
            uint8_t t102 = GM_LD(uint8_t, ((((uint8_t *)t101) + 8) + 0));
            if (t102 != ((uint8_t)20ULL)) goto ie100;
            void * t103 = GM_LD(void *, (((((uint8_t *)t101) + 8) + 8) + 0));
            gm_l18 = t103;
            void * t104 = GM_LD(void *, (((((uint8_t *)t101) + 8) + 8) + 8));
            gm_l19 = t104;
            {
                void * t107 = gm_l16;
                void * t108 = gm_l18;
                uint8_t t109 = gm_f_path_eq_l3qbb7v(t107, t108);
                uint8_t t110 = (uint8_t)!t109;
                if (!t110) goto ie106;
                {
                    uint8_t t112 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                goto ix106;
ie106:;
ix106:;
            }
            {
                void * t114 = gm_l17;
                uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t114) + 8) + 0));
                if (t115 != ((uint8_t)0ULL)) goto ie113;
                gm_tbci5ospk5nnn t116 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t114) + 8) + 8));
                F.l20 = t116;
                {
                    void * t119 = gm_l19;
                    uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t119) + 8) + 0));
                    if (t120 != ((uint8_t)0ULL)) goto ie118;
                    gm_tbci5ospk5nnn t121 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t119) + 8) + 8));
                    F.l21 = t121;
                    {
                        uint8_t t123 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l20)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l21)));
                        uint8_t t124 = 0;
                        F.ret = t123;
                        goto gm_fret;
                    }
                    goto ix118;
ie118:;
ix118:;
                }
                goto ix113;
ie113:;
ix113:;
            }
            {
                void * t125 = gm_l17;
                void * t126 = gm_l19;
                uint8_t t127 = gm_f_path_eq_l3qbb7v(t125, t126);
                uint8_t t128 = 0;
                F.ret = t127;
                goto gm_fret;
            }
            goto ix100;
ie100:;
ix100:;
        }
        {
            uint8_t t129 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn95:;
ma10:;
        uint8_t t131 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn130:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 404, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_t736djc66po0l gm_f_root_oq23mha(void * a0, uint32_t a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_root_oq23mha F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* e */
    uint32_t gm_l3; /* l */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l5; /* local */
    uint32_t gm_l6; /* fr */
    void * gm_l7; /* base */
    void * gm_l8; /* base */
    gm_tao63pk3r18v5 gm_l9; /* sl */
    GM_STACK_CHECK(&F, 463u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma9; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma9; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma9; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma9; case ((uint8_t)45ULL): goto ma9; case ((uint8_t)46ULL): goto ma9; case ((uint8_t)47ULL): goto ma9; case ((uint8_t)48ULL): goto ma9; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma9; case ((uint8_t)51ULL): goto ma9; case ((uint8_t)52ULL): goto ma9; case ((uint8_t)53ULL): goto ma9; case ((uint8_t)54ULL): goto ma9; case ((uint8_t)55ULL): goto ma9; case ((uint8_t)56ULL): goto ma9; case ((uint8_t)57ULL): goto ma9; case ((uint8_t)58ULL): goto ma9; default: goto ma9; }
ma4:;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t13 != ((uint8_t)15ULL)) goto mn12;
        uint32_t t14 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t14;
        uint32_t t15 = gm_l1;
        uint32_t t16 = gm_l3;
        gm_tcc1rq4n1avb2 t17;
        GM_ST(uint32_t, ((uint8_t *)&t17 + 0), t15);
        GM_ST(uint32_t, ((uint8_t *)&t17 + 4), t16);
        gm_t736djc66po0l t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 10, 0);
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t18), t17);
        ((uint8_t *)&t18)[8] = 1;
        uint8_t t19 = 0;
        F.ret = t18;
        goto gm_fret;
        goto me3;
mn12:;
ma5:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)16ULL)) goto mn20;
        uint32_t t22 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t22;
        uint32_t t23 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 4));
        gm_l5 = t23;
        {
            void * t25 = gm_l0;
            void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
            uint32_t t27 = gm_l1;
            uint32_t t28 = gm_l4;
            gm_t9805qtonbsag t29 = gm_f_frame_up_h0ddp1d(t26, t27, t28);
            uint32_t t30;
            if (((((uint8_t *)&t29))[4] != 0)) {
                uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&t29) + 0));
                t30 = t31;
            } else {
                gm_t736djc66po0l t32;
                memset(&t32, 0, sizeof t32);
                uint8_t t33 = 0;
                F.ret = t32;
                goto gm_fret;
            }
            gm_l6 = t30;
        }
        {
            uint32_t t34 = gm_l6;
            uint32_t t35 = gm_l5;
            gm_tcc1rq4n1avb2 t36;
            GM_ST(uint32_t, ((uint8_t *)&t36 + 0), t34);
            GM_ST(uint32_t, ((uint8_t *)&t36 + 4), t35);
            gm_t736djc66po0l t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 10, 0);
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t37), t36);
            ((uint8_t *)&t37)[8] = 1;
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
        goto me3;
mn20:;
ma6:;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t40 != ((uint8_t)19ULL)) goto mn39;
        void * t41 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l7 = t41;
        void * t42 = gm_l0;
        uint32_t t43 = gm_l1;
        void * t44 = gm_l7;
        gm_t736djc66po0l t45 = gm_f_root_oq23mha(t42, t43, t44);
        uint8_t t46 = 0;
        F.ret = t45;
        goto gm_fret;
        goto me3;
mn39:;
ma7:;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t48 != ((uint8_t)20ULL)) goto mn47;
        void * t49 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l8 = t49;
        void * t51 = gm_l0;
        void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
        void * t53 = gm_l8;
        gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t53) + 0));
        uint8_t t55 = gm_f_is_array_p7ehais(t52, GM_CONV(gm_csguankko4lmhd, t54));
        if (!t55) goto ie50;
        {
            void * t57 = gm_l0;
            uint32_t t58 = gm_l1;
            void * t59 = gm_l8;
            gm_t736djc66po0l t60 = gm_f_root_oq23mha(t57, t58, t59);
            uint8_t t61 = 0;
            F.ret = t60;
            goto gm_fret;
        }
        goto ix50;
ie50:;
ix50:;
        goto me3;
mn47:;
ma8:;
        uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t63 != ((uint8_t)21ULL)) goto mn62;
        gm_tao63pk3r18v5 t64 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t2) + 8) + 8));
        gm_l9 = t64;
        void * t66 = gm_l0;
        void * t67 = GM_LD(void *, (((uint8_t *)t66) + 0));
        void * t68 = GM_LD(void *, (((uint8_t *)&gm_l9) + 0));
        gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t68) + 0));
        uint8_t t70 = gm_f_is_array_p7ehais(t67, GM_CONV(gm_csguankko4lmhd, t69));
        if (!t70) goto ie65;
        {
            void * t72 = gm_l0;
            uint32_t t73 = gm_l1;
            void * t74 = GM_LD(void *, (((uint8_t *)&gm_l9) + 0));
            gm_t736djc66po0l t75 = gm_f_root_oq23mha(t72, t73, t74);
            uint8_t t76 = 0;
            F.ret = t75;
            goto gm_fret;
        }
        goto ix65;
ie65:;
ix65:;
        goto me3;
mn62:;
ma9:;
        goto me3;
mn77:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 251, 5);
me3:;
    }
    {
        gm_t736djc66po0l t79;
        memset(&t79, 0, sizeof t79);
        uint8_t t80 = 0;
        F.ret = t79;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_run_checks_c17ks6r(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_run_checks_c17ks6r F;
    void * gm_l0; /* c */
    void * gm_l2; /* s */
    gm_closure gm_l3; /* file_ok */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint32_t gm_l11; /* f */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint32_t gm_l15; /* f */
    void * gm_l16; /* body */
    uint32_t gm_l18; /* outer */
    gm_tf5srh4mp4b3i gm_l19; /* me */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint32_t gm_l22; /* callee */
    uint32_t gm_l23; /* g */
    uint64_t gm_l24; /* d */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* k */
    void * gm_l28; /* init */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint32_t gm_l32; /* callee */
    uint64_t gm_l33; /* d */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint32_t gm_l36; /* f */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* i */
    uint32_t gm_l41; /* f */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    void * gm_l46; /* def */
    uint8_t gm_l47; /* own_params */
    gm_b16_8 gm_l48; /* <tmp> */
    uint64_t gm_l49; /* <tmp> */
    uint64_t gm_l50; /* j */
    uint32_t gm_l52; /* lid */
    uint8_t gm_l53; /* param */
    uint64_t gm_l54; /* writes */
    uint8_t gm_l55; /* mutated */
    void * gm_l56; /* u */
    uint64_t gm_l57; /* uses */
    uint32_t gm_l58; /* n */
    uint64_t gm_l59; /* <tmp> */
    uint64_t gm_l60; /* <tmp> */
    uint64_t gm_l61; /* i */
    void * gm_l62; /* d */
    gm_b16_8 gm_l63; /* name */
    gm_b16_8 gm_l64; /* what */
    uint8_t gm_l66; /* skip */
    gm_b16_8 gm_l67; /* <tmp> */
    uint64_t gm_l68; /* <tmp> */
    gm_b16_8 gm_l71; /* what */
    void * gm_l72; /* inner */
    gm_b16_8 gm_l75; /* <tmp> */
    uint64_t gm_l76; /* <tmp> */
    gm_tl874t2atnlh3 gm_l78; /* span */
    gm_tl874t2atnlh3 gm_l79; /* span */
    gm_tl874t2atnlh3 gm_l80; /* span */
    uint8_t gm_l81; /* keep */
    GM_STACK_CHECK(&F, 5913u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l2 = t3;
    }
    {
        gm_closure t4 = ((gm_closure){(void *)gm_f_file_ok_4j31vl0, (void *)&F.h, cx});
        gm_l3 = t4;
    }
    {
        gm_f_list_fsi5en1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            void * t6 = gm_l2;
            uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
            gm_l5 = t7;
        }
        {
            for (;;) {
                uint64_t t9 = gm_l6;
                uint64_t t10 = gm_l5;
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l6;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l4);
                        gm_tf5srh4mp4b3i t19;
                        memset(&t19, 0, sizeof t19);
                        gm_tftb1odc739t5 t20 = gm_f_push_s4q134v(t18, t19);
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srco9vbah234t6e, 101, 35);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 101, 5);
me15:;
                }
lc8:;
                {
                    uint64_t t23 = gm_l6;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l6 = t26;
                }
            }
lb8:;
        }
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            void * t28 = gm_l2;
            void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
            uint64_t t30 = GM_LD(uintptr_t, ((((uint8_t *)t29) + 240) + 0) + 8);
            gm_l7 = t30;
        }
        {
            for (;;) {
                uint64_t t32 = gm_l8;
                uint64_t t33 = gm_l7;
                uint8_t t34 = (t32 < t33);
                if (!t34) goto lb31;
                {
                    uint64_t t37 = gm_l8;
                    uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&t37));
                    gm_l9 = t40;
                    {
                        void * t43 = gm_l2;
                        uint64_t t44 = gm_l9;
                        gm_tagrkbqh054h7 t45;
                        gm_f_state_a5bnsjb((void *)&t45, (void *)NULL, cx, NULL, t43, t44);
                        if (gm_unw.kind) {
                            goto gm_ll35;
                        }
                        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t45) + 0));
                        if (t46 != ((uint8_t)2ULL)) goto ie42;
                        F.l10 = (*(gm_t6qooom8qa2do *)(((uint8_t *)&t45) + 8));
                        {
                            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                            if (t49 != ((uint8_t)5ULL)) goto ie48;
                            uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)&F.l10) + 8));
                            gm_l11 = t50;
                            {
                                uint32_t t53 = gm_l11;
                                uint64_t t54 = (uint64_t)t53;
                                uint64_t t55 = (uintptr_t)t54;
                                uint64_t t56 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                                if (t55 >= (uintptr_t)(t56)) gm_trap_index(t55, t56, gm_srco9vbah234t6e, 105, 20);
                                uint8_t t57;
                                if (((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t55 * 16))[8] != 0)) goto mn59;
                                t57 = ((uint8_t)1);
                                goto me58;
mn59:;
                                t57 = ((uint8_t)0);
                                goto me58;
mn60:;
                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 105, 20);
me58:;
                                if (!t57) goto ie52;
                                {
                                    uint32_t t62 = gm_l11;
                                    uint64_t t63 = (uint64_t)t62;
                                    uint64_t t64 = (uintptr_t)t63;
                                    uint64_t t65 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                                    if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_srco9vbah234t6e, 105, 54);
                                    uint64_t t66 = gm_l9;
                                    gm_tf5srh4mp4b3i t67;
                                    GM_ST(uint8_t, (uint8_t *)&t67 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t67 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t67 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t67), t66);
                                    ((uint8_t *)&t67)[8] = 1;
                                    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t64 * 16), t67);
                                }
                                goto ix52;
ie52:;
ix52:;
                            }
                            goto ix48;
ie48:;
ix48:;
                        }
                        goto ix42;
ie42:;
ix42:;
                    }
                    goto me38;
mn39:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 102, 5);
me38:;
                }
lc31:;
                {
                    uint64_t t69 = gm_l8;
                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                    uint64_t t72 = r70;
                    gm_l8 = t72;
                }
            }
gm_ll35:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb31; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc31; }
            }
            goto gm_lad;
lb31:;
        }
    }
    {
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            void * t74 = gm_l2;
            uint64_t t75 = GM_LD(uintptr_t, ((((uint8_t *)t74) + 384) + 0) + 8);
            gm_l12 = t75;
        }
        {
            for (;;) {
                uint64_t t77 = gm_l13;
                uint64_t t78 = gm_l12;
                uint8_t t79 = (t77 < t78);
                if (!t79) goto lb76;
                {
                    uint64_t t82 = gm_l13;
                    uint64_t t85 = GM_LD(uint64_t, ((uint8_t *)&t82));
                    gm_l14 = t85;
                    {
                        uint64_t t87 = gm_l14;
                        uint32_t t88 = (uint32_t)t87;
                        gm_l15 = t88;
                    }
                    {
                        uint64_t t90 = gm_l14;
                        uint64_t t91 = (uintptr_t)t90;
                        void * t92 = gm_l2;
                        uint64_t t93 = GM_LD(uintptr_t, ((((uint8_t *)t92) + 344) + 0) + 8);
                        if (t91 >= (uintptr_t)(t93)) gm_trap_index(t91, t93, gm_srco9vbah234t6e, 112, 12);
                        uint8_t t94;
                        if ((((((uint8_t *)GM_LD(void *, ((((uint8_t *)t92) + 344) + 0)) + t91 * 136) + 96))[24] != 0)) goto mn96;
                        t94 = ((uint8_t)0);
                        goto me95;
mn96:;
                        t94 = ((uint8_t)1);
                        goto me95;
mn97:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 112, 12);
me95:;
                        if (!t94) goto ie89;
                        {
                            goto lc76;
                        }
                        goto ix89;
ie89:;
ix89:;
                    }
                    {
                        uint64_t t99 = gm_l14;
                        uint64_t t100 = (uintptr_t)t99;
                        void * t101 = gm_l2;
                        uint64_t t102 = GM_LD(uintptr_t, ((((uint8_t *)t101) + 384) + 0) + 8);
                        if (t100 >= (uintptr_t)(t102)) gm_trap_index(t100, t102, gm_srco9vbah234t6e, 113, 17);
                        void * t103 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t101) + 384) + 0)) + t100 * 64) + 40));
                        void * t104;
                        if ((GM_LD(void *, ((uint8_t *)&t103)) != 0)) {
                            void * t105 = GM_LD(void *, (((uint8_t *)&t103) + 0));
                            t104 = t105;
                        } else {
                            goto lc76;
                        }
                        gm_l16 = t104;
                    }
                    {
                        gm_f_list_go2qg0b((void *)&F.l17, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll80;
                        }
                    }
                    {
                        void * t106 = gm_l0;
                        uint32_t t107 = gm_l15;
                        void * t108 = gm_l16;
                        void * t109 = (void *)((uint8_t *)&F.l17);
                        gm_b0_1 t110 = gm_f_visit_unu9ms6((void *)NULL, cx, NULL, t106, t107, t108, t109);
                        if (gm_unw.kind) {
                            goto gm_ll80;
                        }
                    }
                    {
                        void * t111 = gm_l2;
                        void * t112 = (void *)(((uint8_t *)t111) + 2736);
                        uint32_t t113 = gm_l15;
                        gm_t9805qtonbsag t114 = gm_f_get_q3uemcr(t112, t113);
                        uint32_t t115;
                        if (((((uint8_t *)&t114))[4] != 0)) {
                            uint32_t t116 = GM_LD(uint32_t, (((uint8_t *)&t114) + 0));
                            t115 = t116;
                        } else {
                            uint32_t t117 = gm_l15;
                            t115 = t117;
                        }
                        gm_l18 = t115;
                    }
                    {
                        uint32_t t118 = gm_l18;
                        uint64_t t119 = (uint64_t)t118;
                        uint64_t t120 = (uintptr_t)t119;
                        uint64_t t121 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                        if (t120 >= (uintptr_t)(t121)) gm_trap_index(t120, t121, gm_srco9vbah234t6e, 117, 15);
                        gm_tf5srh4mp4b3i t122 = GM_LD(gm_tf5srh4mp4b3i, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t120 * 16));
                        gm_l19 = t122;
                    }
                    {
                        {
                            gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                            gm_l20 = t124;
                        }
                        {
                            gm_l21 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t126 = gm_l21;
                                uint64_t t127 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                uint8_t t128 = (t126 < t127);
                                if (!t128) goto lb125;
                                {
                                    uint64_t t131 = gm_l21;
                                    uint64_t t132 = (uintptr_t)t131;
                                    uint64_t t133 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                    uint32_t t134 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t132 * 4));
                                    uint32_t t137 = GM_LD(uint32_t, ((uint8_t *)&t134));
                                    gm_l22 = t137;
                                    {
                                        void * t139 = gm_l2;
                                        uint32_t t140 = gm_l22;
                                        uint32_t t141 = gm_f_generic_of_1861hm9(t139, t140);
                                        gm_l23 = t141;
                                    }
                                    {
                                        uint32_t t143 = gm_l23;
                                        uint64_t t144 = (uint64_t)t143;
                                        uint64_t t145 = (uintptr_t)t144;
                                        uint64_t t146 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                                        if (t145 >= (uintptr_t)(t146)) gm_trap_index(t145, t146, gm_srco9vbah234t6e, 120, 21);
                                        if (!((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t145 * 16))[8] != 0)) goto ie142;
                                        uint64_t t147 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t145 * 16));
                                        gm_l24 = t147;
                                        {
                                            gm_tf5srh4mp4b3i t150 = gm_l19;
                                            uint64_t t151 = gm_l24;
                                            gm_tf5srh4mp4b3i t152;
                                            GM_ST(uint8_t, (uint8_t *)&t152 + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t152 + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t152 + 12, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&t152), t151);
                                            ((uint8_t *)&t152)[8] = 1;
                                            uint8_t t153 = (!gm_eq7mri15hk78(((uint8_t *)&t150), ((uint8_t *)&t152)));
                                            if (!t153) goto ie149;
                                            {
                                                uint64_t t155 = gm_l24;
                                                uint64_t t156 = (uintptr_t)t155;
                                                void * t157 = gm_l0;
                                                uint64_t t158 = GM_LD(uintptr_t, ((((uint8_t *)t157) + 72) + 0) + 8);
                                                if (t156 >= (uintptr_t)(t158)) gm_trap_index(t156, t158, gm_srco9vbah234t6e, 121, 34);
                                                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t157) + 72) + 0)) + t156 * 1), ((uint8_t)1));
                                            }
                                            goto ix149;
ie149:;
ix149:;
                                        }
                                        goto ix142;
ie142:;
ix142:;
                                    }
                                    goto me135;
mn136:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 118, 9);
me135:;
                                }
lc125:;
                                {
                                    uint64_t t160 = gm_l21;
                                    uint64_t r161; bool o162 = __builtin_add_overflow(t160, ((uint64_t)1ULL), &r161);
                                    uint64_t t163 = r161;
                                    gm_l21 = t163;
                                }
                            }
lb125:;
                        }
                    }
                    goto me83;
mn84:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 110, 5);
me83:;
                }
lc76:;
                {
                    uint64_t t165 = gm_l13;
                    uint64_t r166; bool o167 = __builtin_add_overflow(t165, ((uint64_t)1ULL), &r166);
                    uint64_t t168 = r166;
                    gm_l13 = t168;
                }
            }
gm_ll80:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb76; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc76; }
            }
            goto gm_lad;
lb76:;
        }
    }
    {
        {
            gm_l26 = ((uint64_t)0ULL);
        }
        {
            void * t170 = gm_l2;
            uint64_t t171 = GM_LD(uintptr_t, ((((uint8_t *)t170) + 1968) + 0) + 8);
            gm_l25 = t171;
        }
        {
            for (;;) {
                uint64_t t173 = gm_l26;
                uint64_t t174 = gm_l25;
                uint8_t t175 = (t173 < t174);
                if (!t175) goto lb172;
                {
                    uint64_t t178 = gm_l26;
                    uint64_t t181 = GM_LD(uint64_t, ((uint8_t *)&t178));
                    gm_l27 = t181;
                    {
                        uint64_t t183 = gm_l27;
                        uint64_t t184 = (uintptr_t)t183;
                        void * t185 = gm_l2;
                        uint64_t t186 = GM_LD(uintptr_t, ((((uint8_t *)t185) + 1968) + 0) + 8);
                        if (t184 >= (uintptr_t)(t186)) gm_trap_index(t184, t186, gm_srco9vbah234t6e, 126, 17);
                        void * t187 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t185) + 1968) + 0)) + t184 * 88) + 64));
                        void * t188;
                        if ((GM_LD(void *, ((uint8_t *)&t187)) != 0)) {
                            void * t189 = GM_LD(void *, (((uint8_t *)&t187) + 0));
                            t188 = t189;
                        } else {
                            goto lc172;
                        }
                        gm_l28 = t188;
                    }
                    {
                        gm_f_list_go2qg0b((void *)&F.l29, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll176;
                        }
                    }
                    {
                        void * t190 = gm_l0;
                        uint32_t t191 = GM_LD(uint32_t, gm_c9b4lh7gft4);
                        void * t192 = gm_l28;
                        void * t193 = (void *)((uint8_t *)&F.l29);
                        gm_b0_1 t194 = gm_f_visit_unu9ms6((void *)NULL, cx, NULL, t190, t191, t192, t193);
                        if (gm_unw.kind) {
                            goto gm_ll176;
                        }
                    }
                    {
                        {
                            gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&F.l29) + 0));
                            gm_l30 = t196;
                        }
                        {
                            gm_l31 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t198 = gm_l31;
                                uint64_t t199 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                                uint8_t t200 = (t198 < t199);
                                if (!t200) goto lb197;
                                {
                                    uint64_t t203 = gm_l31;
                                    uint64_t t204 = (uintptr_t)t203;
                                    uint64_t t205 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                                    uint32_t t206 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t204 * 4));
                                    uint32_t t209 = GM_LD(uint32_t, ((uint8_t *)&t206));
                                    gm_l32 = t209;
                                    {
                                        gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                                        gm_b16_8 t213 = t212;
                                        void * t214 = gm_l2;
                                        uint32_t t215 = gm_l32;
                                        uint32_t t216 = gm_f_generic_of_1861hm9(t214, t215);
                                        uint64_t t217 = (uint64_t)t216;
                                        uint64_t t218 = (uintptr_t)t217;
                                        uint64_t t219 = GM_LD(uintptr_t, ((uint8_t *)&t213) + 8);
                                        if (t218 >= (uintptr_t)(t219)) gm_trap_index(t218, t219, gm_srco9vbah234t6e, 130, 21);
                                        if (!((((uint8_t *)GM_LD(void *, ((uint8_t *)&t213)) + t218 * 16))[8] != 0)) goto ie211;
                                        uint64_t t220 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t213)) + t218 * 16));
                                        gm_l33 = t220;
                                        {
                                            uint64_t t222 = gm_l33;
                                            uint64_t t223 = (uintptr_t)t222;
                                            void * t224 = gm_l0;
                                            uint64_t t225 = GM_LD(uintptr_t, ((((uint8_t *)t224) + 72) + 0) + 8);
                                            if (t223 >= (uintptr_t)(t225)) gm_trap_index(t223, t225, gm_srco9vbah234t6e, 130, 72);
                                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t224) + 72) + 0)) + t223 * 1), ((uint8_t)1));
                                        }
                                        goto ix211;
ie211:;
ix211:;
                                    }
                                    goto me207;
mn208:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 129, 9);
me207:;
                                }
lc197:;
                                {
                                    uint64_t t227 = gm_l31;
                                    uint64_t r228; bool o229 = __builtin_add_overflow(t227, ((uint64_t)1ULL), &r228);
                                    uint64_t t230 = r228;
                                    gm_l31 = t230;
                                }
                            }
lb197:;
                        }
                    }
                    goto me179;
mn180:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 125, 5);
me179:;
                }
lc172:;
                {
                    uint64_t t232 = gm_l26;
                    uint64_t r233; bool o234 = __builtin_add_overflow(t232, ((uint64_t)1ULL), &r233);
                    uint64_t t235 = r233;
                    gm_l26 = t235;
                }
            }
gm_ll176:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb172; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc172; }
            }
            goto gm_lad;
lb172:;
        }
    }
    {
        {
            void * t237 = gm_l2;
            gm_b16_8 t238 = GM_LD(gm_b16_8, ((((uint8_t *)t237) + 920) + 0));
            gm_l34 = t238;
        }
        {
            gm_l35 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t240 = gm_l35;
                uint64_t t241 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                uint8_t t242 = (t240 < t241);
                if (!t242) goto lb239;
                {
                    uint64_t t245 = gm_l35;
                    uint64_t t246 = (uintptr_t)t245;
                    uint64_t t247 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                    uint32_t t248 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t246 * 4));
                    uint32_t t251 = GM_LD(uint32_t, ((uint8_t *)&t248));
                    gm_l36 = t251;
                    {
                        uint32_t t253 = gm_l36;
                        uint64_t t254 = (uint64_t)t253;
                        uint64_t t255 = (uintptr_t)t254;
                        void * t256 = gm_l0;
                        uint64_t t257 = GM_LD(uintptr_t, ((((uint8_t *)t256) + 112) + 0) + 8);
                        if (t255 >= (uintptr_t)(t257)) gm_trap_index(t255, t257, gm_srco9vbah234t6e, 133, 35);
                        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t256) + 112) + 0)) + t255 * 1), ((uint8_t)1));
                    }
                    goto me249;
mn250:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 133, 5);
me249:;
                }
lc239:;
                {
                    uint64_t t259 = gm_l35;
                    uint64_t r260; bool o261 = __builtin_add_overflow(t259, ((uint64_t)1ULL), &r260);
                    uint64_t t262 = r260;
                    gm_l35 = t262;
                }
            }
lb239:;
        }
    }
    {
        void * t263 = gm_l2;
        gm_f_growth_summaries_dguakce((void *)&F.l37, (void *)NULL, cx, NULL, t263);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l39 = ((uint64_t)0ULL);
        }
        {
            void * t265 = gm_l2;
            uint64_t t266 = GM_LD(uintptr_t, ((((uint8_t *)t265) + 384) + 0) + 8);
            gm_l38 = t266;
        }
        {
            for (;;) {
                uint64_t t268 = gm_l39;
                uint64_t t269 = gm_l38;
                uint8_t t270 = (t268 < t269);
                if (!t270) goto lb267;
                {
                    uint64_t t273 = gm_l39;
                    uint64_t t276 = GM_LD(uint64_t, ((uint8_t *)&t273));
                    gm_l40 = t276;
                    {
                        uint64_t t278 = gm_l40;
                        uint32_t t279 = (uint32_t)t278;
                        gm_l41 = t279;
                    }
                    {
                        uint8_t t281 = 0;
                        uint8_t t282 = 0;
                        uint64_t t283 = gm_l40;
                        uint64_t t284 = (uintptr_t)t283;
                        void * t285 = gm_l2;
                        uint64_t t286 = GM_LD(uintptr_t, ((((uint8_t *)t285) + 344) + 0) + 8);
                        if (t284 >= (uintptr_t)(t286)) gm_trap_index(t284, t286, gm_srco9vbah234t6e, 138, 12);
                        uint8_t t287;
                        if ((((((uint8_t *)GM_LD(void *, ((((uint8_t *)t285) + 344) + 0)) + t284 * 136) + 96))[24] != 0)) goto mn289;
                        t287 = ((uint8_t)0);
                        goto me288;
mn289:;
                        t287 = ((uint8_t)1);
                        goto me288;
mn290:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 138, 12);
me288:;
                        if (!t287) {
                            uint64_t t291 = gm_l40;
                            uint64_t t292 = (uintptr_t)t291;
                            void * t293 = gm_l2;
                            uint64_t t294 = GM_LD(uintptr_t, ((((uint8_t *)t293) + 384) + 0) + 8);
                            if (t292 >= (uintptr_t)(t294)) gm_trap_index(t292, t294, gm_srco9vbah234t6e, 138, 51);
                            void * t295 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t293) + 384) + 0)) + t292 * 64) + 40));
                            uint8_t t296;
                            if ((GM_LD(void *, ((uint8_t *)&t295)) != 0)) goto mn298;
                            t296 = ((uint8_t)1);
                            goto me297;
mn298:;
                            t296 = ((uint8_t)0);
                            goto me297;
mn299:;
                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 138, 51);
me297:;
                            t282 = t296;
                        } else t282 = 1;
                        if (!t282) {
                            gm_closure t300 = gm_l3;
                            uint64_t t301 = gm_l40;
                            uint64_t t302 = (uintptr_t)t301;
                            void * t303 = gm_l2;
                            uint64_t t304 = GM_LD(uintptr_t, ((((uint8_t *)t303) + 344) + 0) + 8);
                            if (t302 >= (uintptr_t)(t304)) gm_trap_index(t302, t304, gm_srco9vbah234t6e, 138, 95);
                            gm_tl874t2atnlh3 t305 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t303) + 344) + 0)) + t302 * 136) + 16));
                            gm_closure t306 = t300;
                            gm_node *ccx307 = t306.cx;
                            void * t308 = t306.code;
                            uint8_t t309;
                            if (GM_PTR_TAGGED(t308)) {
                                t309 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t308))(GM_CONV(gm_cseahvg8v7p0vs, t305));
                            } else {
                                t309 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t308)(t306.env, ccx307, NULL, t305);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll271;
                            }
                            uint8_t t310 = (uint8_t)!t309;
                            t281 = t310;
                        } else t281 = 1;
                        if (!t281) goto ie280;
                        {
                            goto lc267;
                        }
                        goto ix280;
ie280:;
ix280:;
                    }
                    {
                        void * t312 = gm_l2;
                        uint64_t t313 = gm_l40;
                        gm_f_possible_dangling_addres_d403vr0((void *)&F.l42, (void *)NULL, cx, NULL, t312, t313);
                        if (gm_unw.kind) {
                            goto gm_ll271;
                        }
                    }
                    {
                        {
                            gm_b16_8 t315 = GM_LD(gm_b16_8, (((uint8_t *)&F.l42) + 0));
                            gm_l43 = t315;
                        }
                        {
                            gm_l44 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t317 = gm_l44;
                                uint64_t t318 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                                uint8_t t319 = (t317 < t318);
                                if (!t319) goto lb316;
                                {
                                    uint64_t t322 = gm_l44;
                                    uint64_t t323 = (uintptr_t)t322;
                                    uint64_t t324 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                                    gm_tg9l3k9j9m9b9 t327 = GM_LD(gm_tg9l3k9j9m9b9, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t323 * 72));
                                    F.l45 = t327;
                                    {
                                        uint8_t t330 = GM_LD(uint8_t, (((uint8_t *)&F.l45) + 64));
                                        uint8_t t331 = (uint8_t)!t330;
                                        if (!t331) goto ie329;
                                        {
                                            void * t333 = gm_l0;
                                            void * t334 = (void *)(((uint8_t *)t333) + 208);
                                            gm_tllos8achgacq t335;
                                            memset((uint8_t *)&t335 + 12, 0, 4);
                                            memset((uint8_t *)&t335 + 44, 0, 4);
                                            gm_tl874t2atnlh3 t336 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l45) + 0));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t335 + 0, t336);
                                            gm_b16_8 t337 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 16));
                                            GM_ST(gm_b16_8, (uint8_t *)&t335 + 16, t337);
                                            gm_tl874t2atnlh3 t338 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l45) + 32));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t335 + 32, t338);
                                            gm_b16_8 t339 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 48));
                                            GM_ST(gm_b16_8, (uint8_t *)&t335 + 48, t339);
                                            gm_tj5f8vajf35o7 t340;
                                            GM_ST(uint8_t, (uint8_t *)&t340 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t340 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t340 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t340 + 0), ((uint8_t)7ULL));
                                            GM_ST(gm_tllos8achgacq, ((uint8_t *)&t340 + 8), t335);
                                            gm_tftb1odc739t5 t341 = gm_f_push_rqlbv1e(t334, &t340);
                                            if (((((uint8_t *)&t341))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t341) + 1, gm_srco9vbah234t6e, 141, 32);
                                            gm_b0_1 t342 = GM_LD(gm_b0_1, (((uint8_t *)&t341) + 1));
                                        }
                                        goto ix329;
ie329:;
ix329:;
                                    }
                                    goto me325;
mn326:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 140, 9);
me325:;
                                }
lc316:;
                                {
                                    uint64_t t344 = gm_l44;
                                    uint64_t r345; bool o346 = __builtin_add_overflow(t344, ((uint64_t)1ULL), &r345);
                                    uint64_t t347 = r345;
                                    gm_l44 = t347;
                                }
                            }
lb316:;
                        }
                    }
                    {
                        void * t348 = gm_l2;
                        uint64_t t349 = gm_l40;
                        void * t350 = gm_l0;
                        void * t351 = (void *)(((uint8_t *)t350) + 208);
                        gm_b0_1 t352 = gm_f_mutated_copies_vc6lpss((void *)NULL, cx, NULL, t348, t349, t351);
                        if (gm_unw.kind) {
                            goto gm_ll271;
                        }
                    }
                    {
                        void * t353 = gm_l2;
                        uint64_t t354 = gm_l40;
                        gm_b16_8 t355 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 0));
                        gm_b16_8 t356 = GM_LD(gm_b16_8, ((uint8_t *)&t355));
                        void * t357 = gm_l0;
                        void * t358 = (void *)(((uint8_t *)t357) + 208);
                        gm_b0_1 t359 = gm_f_stale_element_pointers_jjgn0ll((void *)NULL, cx, NULL, t353, t354, t356, t358);
                        if (gm_unw.kind) {
                            goto gm_ll271;
                        }
                    }
                    {
                        uint64_t t360 = gm_l40;
                        uint64_t t361 = (uintptr_t)t360;
                        void * t362 = gm_l2;
                        uint64_t t363 = GM_LD(uintptr_t, ((((uint8_t *)t362) + 384) + 0) + 8);
                        if (t361 >= (uintptr_t)(t363)) gm_trap_index(t361, t363, gm_srco9vbah234t6e, 145, 17);
                        void * t364 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t362) + 384) + 0)) + t361 * 64);
                        gm_l46 = t364;
                    }
                    {
                        uint8_t t365 = 0;
                        uint8_t t366 = 0;
                        void * t367 = gm_l46;
                        uint8_t t368 = ((GM_LD(uint8_t, (((uint8_t *)t367) + 0) + 0) == ((uint8_t)0ULL)));
                        if (t368) {
                            uint64_t t369 = gm_l40;
                            uint64_t t370 = (uintptr_t)t369;
                            void * t371 = gm_l0;
                            uint64_t t372 = GM_LD(uintptr_t, ((((uint8_t *)t371) + 112) + 0) + 8);
                            if (t370 >= (uintptr_t)(t372)) gm_trap_index(t370, t372, gm_srco9vbah234t6e, 146, 50);
                            uint8_t t373 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t371) + 112) + 0)) + t370 * 1));
                            uint8_t t374 = (uint8_t)!t373;
                            t366 = t374;
                        } else t366 = 0;
                        if (t366) {
                            uint64_t t375 = gm_l40;
                            uint64_t t376 = (uintptr_t)t375;
                            void * t377 = gm_l2;
                            uint64_t t378 = GM_LD(uintptr_t, ((((uint8_t *)t377) + 344) + 0) + 8);
                            if (t376 >= (uintptr_t)(t378)) gm_trap_index(t376, t378, gm_srco9vbah234t6e, 146, 74);
                            uint8_t t379 = (!((GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t377) + 344) + 0)) + t376 * 136) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t377) + 344) + 0)) + t376 * 136) + 0)), gm_sog4rksrmsqd2, 4) == 0));
                            t365 = t379;
                        } else t365 = 0;
                        gm_l47 = t365;
                    }
                    {
                        {
                            void * t381 = gm_l46;
                            gm_b16_8 t382 = GM_LD(gm_b16_8, (((uint8_t *)t381) + 24));
                            gm_b16_8 t383 = GM_LD(gm_b16_8, ((uint8_t *)&t382));
                            gm_l48 = t383;
                        }
                        {
                            gm_l49 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t385 = gm_l49;
                                uint64_t t386 = GM_LD(uintptr_t, ((uint8_t *)&gm_l48) + 8);
                                uint8_t t387 = (t385 < t386);
                                if (!t387) goto lb384;
                                {
                                    gm_t5im2v50m3hk6 t390;
                                    uint64_t t391 = gm_l49;
                                    GM_ST(uint64_t, (uint8_t *)&t390 + 0, t391);
                                    uint64_t t392 = gm_l49;
                                    uint64_t t393 = (uintptr_t)t392;
                                    uint64_t t394 = GM_LD(uintptr_t, ((uint8_t *)&gm_l48) + 8);
                                    memcpy((uint8_t *)&t390 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l48)) + t393 * 40), 40);
                                    uint64_t t397 = GM_LD(uint64_t, (((uint8_t *)&t390) + 0));
                                    gm_l50 = t397;
                                    F.l51 = (*(gm_tsoie1ppgdvcg *)(((uint8_t *)&t390) + 8));
                                    {
                                        uint64_t t399 = gm_l50;
                                        uint32_t t400 = (uint32_t)t399;
                                        gm_l52 = t400;
                                    }
                                    {
                                        uint8_t t402 = 0;
                                        uint8_t t403 = 0;
                                        uint8_t t404 = 0;
                                        gm_tl874t2atnlh3 t405 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                        gm_tl874t2atnlh3 t406 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                                        uint8_t t407 = (gm_eqff85ac10o3(((uint8_t *)&t405), ((uint8_t *)&t406)));
                                        if (!t407) {
                                            gm_b16_8 t408 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
                                            gm_b16_8 t409;
                                            GM_ST(void *, (uint8_t *)&t409, (void *)(gm_s1f440st4mnkp));
                                            GM_ST(uintptr_t, (uint8_t *)&t409 + 8, (uintptr_t)(1));
                                            uint8_t t410 = gm_f_starts_with_6vvt01e(t408, t409);
                                            t404 = t410;
                                        } else t404 = 1;
                                        if (!t404) {
                                            gm_b16_8 t411 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
                                            gm_b16_8 t412;
                                            GM_ST(void *, (uint8_t *)&t412, (void *)(gm_sfdbfrv46lihd));
                                            GM_ST(uintptr_t, (uint8_t *)&t412 + 8, (uintptr_t)(1));
                                            uint8_t t413 = gm_f_starts_with_6vvt01e(t411, t412);
                                            t403 = t413;
                                        } else t403 = 1;
                                        if (!t403) {
                                            gm_closure t414 = gm_l3;
                                            gm_tl874t2atnlh3 t415 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                            gm_closure t416 = t414;
                                            gm_node *ccx417 = t416.cx;
                                            void * t418 = t416.code;
                                            uint8_t t419;
                                            if (GM_PTR_TAGGED(t418)) {
                                                t419 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t418))(GM_CONV(gm_cseahvg8v7p0vs, t415));
                                            } else {
                                                t419 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t418)(t416.env, ccx417, NULL, t415);
                                            }
                                            if (gm_unw.kind) {
                                                goto gm_ll388;
                                            }
                                            uint8_t t420 = (uint8_t)!t419;
                                            t402 = t420;
                                        } else t402 = 1;
                                        if (!t402) goto ie401;
                                        {
                                            goto lc384;
                                        }
                                        goto ix401;
ie401:;
ix401:;
                                    }
                                    {
                                        uint32_t t422 = gm_l52;
                                        void * t423 = gm_l46;
                                        gm_b16_8 t424 = GM_LD(gm_b16_8, (((uint8_t *)t423) + 8));
                                        uint8_t t425 = 0;
                                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t424) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t424))) + i * 4, ((uint8_t *)&t422))) { t425 = 1; break; }
                                        uint8_t t426 = (uint8_t)(t425);
                                        gm_l53 = t426;
                                    }
                                    {
                                        uint8_t t428 = gm_l53;
                                        if (!t428) goto ie427;
                                        uint8_t t429 = gm_l47;
                                        uint8_t t430 = (uint8_t)!t429;
                                        if (!t430) goto ie427;
                                        {
                                            goto lc384;
                                        }
                                        goto ix427;
ie427:;
ix427:;
                                    }
                                    {
                                        uint8_t t433 = gm_l53;
                                        uint8_t t434 = (uint8_t)!t433;
                                        if (!t434) goto ie432;
                                        void * t435 = gm_l0;
                                        void * t436 = (void *)(((uint8_t *)t435) + 152);
                                        uint32_t t437 = gm_l41;
                                        uint32_t t438 = gm_l52;
                                        gm_tcc1rq4n1avb2 t439;
                                        GM_ST(uint32_t, ((uint8_t *)&t439 + 0), t437);
                                        GM_ST(uint32_t, ((uint8_t *)&t439 + 4), t438);
                                        uint8_t t440 = gm_f_contains_14ai471(t436, t439);
                                        uint8_t t441 = (uint8_t)!t440;
                                        if (!t441) goto ie432;
                                        {
                                            goto lc384;
                                        }
                                        goto ix432;
ie432:;
ix432:;
                                    }
                                    {
                                        gm_l54 = ((uint64_t)0ULL);
                                    }
                                    {
                                        gm_l55 = ((uint8_t)0);
                                    }
                                    {
                                        void * t444 = gm_l0;
                                        void * t445 = (void *)(((uint8_t *)t444) + 16);
                                        uint32_t t446 = gm_l41;
                                        uint32_t t447 = gm_l52;
                                        gm_tcc1rq4n1avb2 t448;
                                        GM_ST(uint32_t, ((uint8_t *)&t448 + 0), t446);
                                        GM_ST(uint32_t, ((uint8_t *)&t448 + 4), t447);
                                        void * t449 = gm_f_get_ptr_1a4voen(t445, t448);
                                        if (!(GM_LD(void *, ((uint8_t *)&t449)) != 0)) goto ie443;
                                        void * t450 = GM_LD(void *, ((uint8_t *)&t449));
                                        gm_l56 = t450;
                                        {
                                            void * t452 = gm_l56;
                                            uint64_t t453 = GM_LD(uintptr_t, ((((uint8_t *)t452) + 0) + 0) + 8);
                                            gm_l54 = t453;
                                        }
                                        {
                                            void * t454 = gm_l56;
                                            uint8_t t455 = GM_LD(uint8_t, (((uint8_t *)t454) + 40));
                                            gm_l55 = t455;
                                        }
                                        goto ix443;
ie443:;
ix443:;
                                    }
                                    {
                                        gm_l57 = ((uint64_t)0ULL);
                                    }
                                    {
                                        void * t457 = gm_l2;
                                        void * t458 = (void *)(((uint8_t *)t457) + 2680);
                                        gm_tl874t2atnlh3 t459 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                        gm_t9805qtonbsag t460 = gm_f_get_rlnefnq(t458, GM_CONV(gm_cseahvg8v7p0vs, t459));
                                        if (!((((uint8_t *)&t460))[4] != 0)) goto ie456;
                                        uint32_t t461 = GM_LD(uint32_t, ((uint8_t *)&t460));
                                        gm_l58 = t461;
                                        {
                                            uint32_t t463 = gm_l58;
                                            uint64_t t464 = (uint64_t)t463;
                                            gm_l57 = t464;
                                        }
                                        goto ix456;
ie456:;
ix456:;
                                    }
                                    {
                                        uint64_t t466 = gm_l57;
                                        uint64_t t467 = gm_l54;
                                        uint8_t t468 = ((t466 == t467));
                                        if (!t468) goto ie465;
                                        {
                                            void * t470 = gm_l0;
                                            void * t471 = (void *)(((uint8_t *)t470) + 208);
                                            gm_tppdoi1slt2pr t472;
                                            memset((uint8_t *)&t472 + 30, 0, 2);
                                            gm_b16_8 t473 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t472 + 0, t473);
                                            gm_tl874t2atnlh3 t474 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t472 + 16, t474);
                                            uint8_t t475 = gm_l53;
                                            GM_ST(uint8_t, (uint8_t *)&t472 + 28, t475);
                                            uint64_t t476 = gm_l54;
                                            uint8_t t477 = (t476 > ((uint64_t)0ULL));
                                            GM_ST(uint8_t, (uint8_t *)&t472 + 29, t477);
                                            gm_tj5f8vajf35o7 t478;
                                            memset(&t478, 0, sizeof t478);
                                            GM_ST(uint8_t, ((uint8_t *)&t478 + 0), ((uint8_t)0ULL));
                                            GM_ST(gm_tppdoi1slt2pr, ((uint8_t *)&t478 + 8), t472);
                                            gm_tftb1odc739t5 t479 = gm_f_push_rqlbv1e(t471, &t478);
                                            if (((((uint8_t *)&t479))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t479) + 1, gm_srco9vbah234t6e, 162, 17);
                                            gm_b0_1 t480 = GM_LD(gm_b0_1, (((uint8_t *)&t479) + 1));
                                        }
                                        goto ix465;
ie465:;
                                        uint8_t t482 = GM_LD(uint8_t, (((uint8_t *)&F.l51) + 20));
                                        if (!t482) goto ie481;
                                        uint8_t t483 = gm_l53;
                                        uint8_t t484 = (uint8_t)!t483;
                                        if (!t484) goto ie481;
                                        uint8_t t485 = gm_l55;
                                        uint8_t t486 = (uint8_t)!t485;
                                        if (!t486) goto ie481;
                                        uint64_t t487 = gm_l54;
                                        uint8_t t488 = ((t487 == ((uint64_t)0ULL)));
                                        if (!t488) goto ie481;
                                        {
                                            void * t490 = gm_l0;
                                            void * t491 = (void *)(((uint8_t *)t490) + 208);
                                            gm_t08olq97rscn3 t492;
                                            memset((uint8_t *)&t492 + 28, 0, 4);
                                            gm_b16_8 t493 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t492 + 0, t493);
                                            gm_tl874t2atnlh3 t494 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t492 + 16, t494);
                                            gm_tj5f8vajf35o7 t495;
                                            memset(&t495, 0, sizeof t495);
                                            GM_ST(uint8_t, ((uint8_t *)&t495 + 0), ((uint8_t)1ULL));
                                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t495 + 8), t492);
                                            gm_tftb1odc739t5 t496 = gm_f_push_rqlbv1e(t491, &t495);
                                            if (((((uint8_t *)&t496))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t496) + 1, gm_srco9vbah234t6e, 164, 17);
                                            gm_b0_1 t497 = GM_LD(gm_b0_1, (((uint8_t *)&t496) + 1));
                                        }
                                        goto ix481;
ie481:;
ix481:;
ix465:;
                                    }
                                    goto me395;
mn396:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 147, 9);
me395:;
                                }
lc384:;
                                {
                                    uint64_t t499 = gm_l49;
                                    uint64_t r500; bool o501 = __builtin_add_overflow(t499, ((uint64_t)1ULL), &r500);
                                    uint64_t t502 = r500;
                                    gm_l49 = t502;
                                }
                            }
gm_ll388:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 9) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb384; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc384; }
                            }
                            goto gm_ll271;
lb384:;
                        }
                    }
                    goto me274;
mn275:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 136, 5);
me274:;
                }
lc267:;
                {
                    uint64_t t504 = gm_l39;
                    uint64_t r505; bool o506 = __builtin_add_overflow(t504, ((uint64_t)1ULL), &r505);
                    uint64_t t507 = r505;
                    gm_l39 = t507;
                }
            }
gm_ll271:;
            if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb267; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc267; }
            }
            goto gm_lad;
lb267:;
        }
    }
    {
        {
            gm_l60 = ((uint64_t)0ULL);
        }
        {
            void * t509 = gm_l2;
            void * t510 = GM_LD(void *, (((uint8_t *)t509) + 0));
            uint64_t t511 = GM_LD(uintptr_t, ((((uint8_t *)t510) + 240) + 0) + 8);
            gm_l59 = t511;
        }
        {
            for (;;) {
                uint64_t t513 = gm_l60;
                uint64_t t514 = gm_l59;
                uint8_t t515 = (t513 < t514);
                if (!t515) goto lb512;
                {
                    uint64_t t518 = gm_l60;
                    uint64_t t521 = GM_LD(uint64_t, ((uint8_t *)&t518));
                    gm_l61 = t521;
                    {
                        uint64_t t523 = gm_l61;
                        uint64_t t524 = (uintptr_t)t523;
                        void * t525 = gm_l2;
                        void * t526 = GM_LD(void *, (((uint8_t *)t525) + 0));
                        uint64_t t527 = GM_LD(uintptr_t, ((((uint8_t *)t526) + 240) + 0) + 8);
                        if (t524 >= (uintptr_t)(t527)) gm_trap_index(t524, t527, gm_srco9vbah234t6e, 170, 15);
                        void * t528 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t526) + 240) + 0)) + t524 * 216);
                        gm_l62 = t528;
                    }
                    {
                        void * t529 = gm_l62;
                        gm_b16_8 t530 = GM_LD(gm_b16_8, ((((uint8_t *)t529) + 0) + 0));
                        gm_l63 = t530;
                    }
                    {
                        void * t532 = gm_l0;
                        uint64_t t533 = gm_l61;
                        gm_b16_8 t534 = F.l1;
                        gm_th2lqbo77pvsc t535;
                        gm_f_needless_pub_jqsnq13((void *)&t535, (void *)NULL, cx, NULL, t532, t533, t534);
                        if (gm_unw.kind) {
                            goto gm_ll516;
                        }
                        if (!((((uint8_t *)&t535))[16] != 0)) goto ie531;
                        gm_b16_8 t536 = GM_LD(gm_b16_8, ((uint8_t *)&t535));
                        gm_l64 = t536;
                        {
                            void * t538 = gm_l0;
                            void * t539 = (void *)(((uint8_t *)t538) + 208);
                            gm_t26d2uc07m5o2 t540;
                            memset((uint8_t *)&t540 + 29, 0, 3);
                            void * t541 = gm_l62;
                            gm_b16_8 t542 = GM_LD(gm_b16_8, ((((uint8_t *)t541) + 0) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t540 + 0, t542);
                            void * t543 = gm_l62;
                            gm_tl874t2atnlh3 t544 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t543) + 0) + 16));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t540 + 16, t544);
                            void * t545 = gm_l62;
                            gm_t9qkh0rsooc8d t546 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t545) + 40) + 56));
                            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t540 + 28, t546);
                            gm_b16_8 t547 = gm_l64;
                            GM_ST(gm_b16_8, (uint8_t *)&t540 + 32, t547);
                            gm_tj5f8vajf35o7 t548;
                            GM_ST(uint8_t, (uint8_t *)&t548 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t548 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t548 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t548 + 56, 0);
                            GM_ST(uint64_t, (uint8_t *)&t548 + 64, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t548 + 0), ((uint8_t)6ULL));
                            GM_ST(gm_t26d2uc07m5o2, ((uint8_t *)&t548 + 8), t540);
                            gm_tftb1odc739t5 t549 = gm_f_push_rqlbv1e(t539, &t548);
                            if (((((uint8_t *)&t549))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t549) + 1, gm_srco9vbah234t6e, 173, 13);
                            gm_b0_1 t550 = GM_LD(gm_b0_1, (((uint8_t *)&t549) + 1));
                        }
                        goto ix531;
ie531:;
ix531:;
                    }
                    {
                        uint8_t t552 = 0;
                        uint8_t t553 = 0;
                        uint8_t t554 = 0;
                        uint8_t t555 = 0;
                        uint8_t t556 = 0;
                        uint8_t t557 = 0;
                        void * t558 = gm_l62;
                        uint8_t t559 = ((GM_LD(uint8_t, ((((uint8_t *)t558) + 40) + 56) + 0) == ((uint8_t)2ULL)));
                        if (!t559) {
                            uint8_t t560 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l63) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l63)), gm_sog4rksrmsqd2, 4) == 0));
                            t557 = t560;
                        } else t557 = 1;
                        if (!t557) {
                            gm_b16_8 t561 = gm_l63;
                            gm_b16_8 t562;
                            GM_ST(void *, (uint8_t *)&t562, (void *)(gm_sfdbfrv46lihd));
                            GM_ST(uintptr_t, (uint8_t *)&t562 + 8, (uintptr_t)(1));
                            uint8_t t563 = gm_f_starts_with_6vvt01e(t561, t562);
                            t556 = t563;
                        } else t556 = 1;
                        if (!t556) {
                            void * t564 = gm_l62;
                            uint64_t t565 = GM_LD(uint64_t, (((uint8_t *)t564) + 32));
                            uint64_t t566 = (uintptr_t)t565;
                            void * t567 = gm_l2;
                            void * t568 = GM_LD(void *, (((uint8_t *)t567) + 0));
                            uint64_t t569 = GM_LD(uintptr_t, ((((uint8_t *)t568) + 200) + 0) + 8);
                            if (t566 >= (uintptr_t)(t569)) gm_trap_index(t566, t569, gm_srco9vbah234t6e, 175, 80);
                            uint8_t t570 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t568) + 200) + 0)) + t566 * 184) + 112));
                            t555 = t570;
                        } else t555 = 1;
                        if (!t555) {
                            gm_closure t571 = gm_l3;
                            void * t572 = gm_l62;
                            gm_tl874t2atnlh3 t573 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t572) + 0) + 16));
                            gm_closure t574 = t571;
                            gm_node *ccx575 = t574.cx;
                            void * t576 = t574.code;
                            uint8_t t577;
                            if (GM_PTR_TAGGED(t576)) {
                                t577 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t576))(GM_CONV(gm_cseahvg8v7p0vs, t573));
                            } else {
                                t577 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t576)(t574.env, ccx575, NULL, t573);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll516;
                            }
                            uint8_t t578 = (uint8_t)!t577;
                            t554 = t578;
                        } else t554 = 1;
                        if (!t554) {
                            void * t579 = gm_l2;
                            uint64_t t580 = gm_l61;
                            uint8_t t581 = gm_f_is_decl_used_oldf2ja(t579, t580);
                            t553 = t581;
                        } else t553 = 1;
                        if (!t553) {
                            uint64_t t582 = gm_l61;
                            uint64_t t583 = (uintptr_t)t582;
                            void * t584 = gm_l0;
                            uint64_t t585 = GM_LD(uintptr_t, ((((uint8_t *)t584) + 72) + 0) + 8);
                            if (t583 >= (uintptr_t)(t585)) gm_trap_index(t583, t585, gm_srco9vbah234t6e, 175, 171);
                            uint8_t t586 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t584) + 72) + 0)) + t583 * 1));
                            t552 = t586;
                        } else t552 = 1;
                        if (!t552) goto ie551;
                        {
                            goto lc512;
                        }
                        goto ix551;
ie551:;
ix551:;
                    }
                    {
                        void * t589 = gm_l62;
                        uint8_t t590 = GM_LD(uint8_t, (((((uint8_t *)t589) + 40) + 64) + 0));
                        if (t590 != ((uint8_t)0ULL)) goto ie588;
                        gm_tv2uf4leil5hq t591 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t589) + 40) + 64) + 8));
                        F.l65 = t591;
                        {
                            gm_l66 = ((uint8_t)0);
                        }
                        {
                            {
                                void * t594 = gm_l62;
                                gm_b16_8 t595 = GM_LD(gm_b16_8, ((((uint8_t *)t594) + 40) + 40));
                                gm_l67 = t595;
                            }
                            {
                                gm_l68 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t597 = gm_l68;
                                    uint64_t t598 = GM_LD(uintptr_t, ((uint8_t *)&gm_l67) + 8);
                                    uint8_t t599 = (t597 < t598);
                                    if (!t599) goto lb596;
                                    {
                                        uint64_t t602 = gm_l68;
                                        uint64_t t603 = (uintptr_t)t602;
                                        uint64_t t604 = GM_LD(uintptr_t, ((uint8_t *)&gm_l67) + 8);
                                        gm_tkacitepg5fk0 t607 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l67)) + t603 * 72));
                                        F.l69 = t607;
                                        {
                                            gm_b16_8 t610 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l69) + 0) + 0));
                                            gm_t6j6smj7qoctu t611;
                                            gm_b16_8 t612;
                                            GM_ST(void *, (uint8_t *)&t612, (void *)(gm_spe8t3e79vnal));
                                            GM_ST(uintptr_t, (uint8_t *)&t612 + 8, (uintptr_t)(6));
                                            GM_ST(gm_b16_8, (uint8_t *)&t611 + 0, t612);
                                            gm_b16_8 t613;
                                            GM_ST(void *, (uint8_t *)&t613, (void *)(gm_s1ft2nrvnhcbp));
                                            GM_ST(uintptr_t, (uint8_t *)&t613 + 8, (uintptr_t)(9));
                                            GM_ST(gm_b16_8, (uint8_t *)&t611 + 16, t613);
                                            uint8_t t614 = 0;
                                            if (gm_eq0iqiubfm7e(((uint8_t *)&t611) + 0, ((uint8_t *)&t610))) t614 = 1;
                                            if (gm_eq0iqiubfm7e(((uint8_t *)&t611) + 16, ((uint8_t *)&t610))) t614 = 1;
                                            uint8_t t615 = (uint8_t)(t614);
                                            if (!t615) goto ie609;
                                            {
                                                gm_l66 = ((uint8_t)1);
                                            }
                                            goto ix609;
ie609:;
ix609:;
                                        }
                                        goto me605;
mn606:;
                                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 178, 9);
me605:;
                                    }
lc596:;
                                    {
                                        uint64_t t618 = gm_l68;
                                        uint64_t r619; bool o620 = __builtin_add_overflow(t618, ((uint64_t)1ULL), &r619);
                                        uint64_t t621 = r619;
                                        gm_l68 = t621;
                                    }
                                }
lb596:;
                            }
                        }
                        {
                            uint8_t t623 = gm_l66;
                            if (!t623) goto ie622;
                            {
                                goto lc512;
                            }
                            goto ix622;
ie622:;
ix622:;
                        }
                        {
                            void * t625 = gm_l2;
                            uint64_t t626 = gm_l61;
                            gm_f_state_a5bnsjb((void *)&F.l70, (void *)NULL, cx, NULL, t625, t626);
                            if (gm_unw.kind) {
                                goto gm_ll516;
                            }
                        }
                        {
                            uint8_t t628;
                            uint8_t t631 = GM_LD(uint8_t, (((uint8_t *)&F.l70) + 0));
                            if (t631 != ((uint8_t)2ULL)) goto mn630;
                            t628 = ((uint8_t)0);
                            goto me629;
mn630:;
                            t628 = ((uint8_t)1);
                            goto me629;
mn632:;
                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 183, 12);
me629:;
                            if (!t628) goto ie627;
                            {
                                goto lc512;
                            }
                            goto ix627;
ie627:;
ix627:;
                        }
                        {
                            gm_b16_8 t634;
                            GM_ST(void *, (uint8_t *)&t634, (void *)(gm_svklio0qebpog));
                            GM_ST(uintptr_t, (uint8_t *)&t634 + 8, (uintptr_t)(8));
                            gm_l71 = t634;
                        }
                        {
                            void * t635 = GM_LD(void *, (((uint8_t *)&F.l65) + 40));
                            gm_t2gtbh39ptekd t636 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t635) + 8));
                            uint8_t t646 = GM_LD(uint8_t, (((uint8_t *)&t636) + 0));
                            switch (t646) { case ((uint8_t)0ULL): goto ma644; case ((uint8_t)1ULL): goto ma644; case ((uint8_t)2ULL): goto ma644; case ((uint8_t)3ULL): goto ma644; case ((uint8_t)4ULL): goto ma644; case ((uint8_t)5ULL): goto ma644; case ((uint8_t)6ULL): goto ma644; case ((uint8_t)7ULL): goto ma644; case ((uint8_t)8ULL): goto ma644; case ((uint8_t)9ULL): goto ma644; case ((uint8_t)10ULL): goto ma644; case ((uint8_t)11ULL): goto ma644; case ((uint8_t)12ULL): goto ma644; case ((uint8_t)13ULL): goto ma644; case ((uint8_t)14ULL): goto ma644; case ((uint8_t)15ULL): goto ma644; case ((uint8_t)16ULL): goto ma644; case ((uint8_t)17ULL): goto ma644; case ((uint8_t)18ULL): goto ma644; case ((uint8_t)19ULL): goto ma644; case ((uint8_t)20ULL): goto ma644; case ((uint8_t)21ULL): goto ma644; case ((uint8_t)22ULL): goto ma644; case ((uint8_t)23ULL): goto ma644; case ((uint8_t)24ULL): goto ma644; case ((uint8_t)25ULL): goto ma644; case ((uint8_t)26ULL): goto ma644; case ((uint8_t)27ULL): goto ma644; case ((uint8_t)28ULL): goto ma644; case ((uint8_t)29ULL): goto ma644; case ((uint8_t)30ULL): goto ma644; case ((uint8_t)31ULL): goto ma644; case ((uint8_t)32ULL): goto ma641; case ((uint8_t)33ULL): goto ma641; case ((uint8_t)34ULL): goto ma642; case ((uint8_t)35ULL): goto ma643; case ((uint8_t)36ULL): goto ma638; case ((uint8_t)37ULL): goto ma644; case ((uint8_t)38ULL): goto ma639; case ((uint8_t)39ULL): goto ma640; case ((uint8_t)40ULL): goto ma644; case ((uint8_t)41ULL): goto ma644; case ((uint8_t)42ULL): goto ma644; case ((uint8_t)43ULL): goto ma644; case ((uint8_t)44ULL): goto ma644; case ((uint8_t)45ULL): goto ma644; case ((uint8_t)46ULL): goto ma644; case ((uint8_t)47ULL): goto ma644; case ((uint8_t)48ULL): goto ma644; case ((uint8_t)49ULL): goto ma644; case ((uint8_t)50ULL): goto ma644; default: goto ma644; }
ma638:;
                            uint8_t t648 = GM_LD(uint8_t, (((uint8_t *)&t636) + 0));
                            if (t648 != ((uint8_t)36ULL)) goto mn647;
                            {
                                gm_b16_8 t650;
                                GM_ST(void *, (uint8_t *)&t650, (void *)(gm_sq0dqiq83b924));
                                GM_ST(uintptr_t, (uint8_t *)&t650 + 8, (uintptr_t)(8));
                                gm_l71 = t650;
                            }
                            goto me637;
mn647:;
ma639:;
                            uint8_t t652 = GM_LD(uint8_t, (((uint8_t *)&t636) + 0));
                            if (t652 != ((uint8_t)38ULL)) goto mn651;
                            {
                                gm_b16_8 t654;
                                GM_ST(void *, (uint8_t *)&t654, (void *)(gm_sg3kpa3h16no3));
                                GM_ST(uintptr_t, (uint8_t *)&t654 + 8, (uintptr_t)(6));
                                gm_l71 = t654;
                            }
                            goto me637;
mn651:;
ma640:;
                            uint8_t t656 = GM_LD(uint8_t, (((uint8_t *)&t636) + 0));
                            if (t656 != ((uint8_t)39ULL)) goto mn655;
                            {
                                gm_b16_8 t658;
                                GM_ST(void *, (uint8_t *)&t658, (void *)(gm_sa0amdcmvej1l));
                                GM_ST(uintptr_t, (uint8_t *)&t658 + 8, (uintptr_t)(13));
                                gm_l71 = t658;
                            }
                            goto me637;
mn655:;
ma641:;
                            uint8_t t662 = GM_LD(uint8_t, (((uint8_t *)&t636) + 0));
                            if (t662 != ((uint8_t)32ULL)) goto pa661;
                            goto po660;
pa661:;
                            uint8_t t664 = GM_LD(uint8_t, (((uint8_t *)&t636) + 0));
                            if (t664 != ((uint8_t)33ULL)) goto pa663;
                            goto po660;
pa663:;
                            goto mn659;
po660:;
                            {
                                gm_b16_8 t666;
                                GM_ST(void *, (uint8_t *)&t666, (void *)(gm_s2737i1iq57nf));
                                GM_ST(uintptr_t, (uint8_t *)&t666 + 8, (uintptr_t)(4));
                                gm_l71 = t666;
                            }
                            goto me637;
mn659:;
ma642:;
                            uint8_t t668 = GM_LD(uint8_t, (((uint8_t *)&t636) + 0));
                            if (t668 != ((uint8_t)34ULL)) goto mn667;
                            {
                                gm_b16_8 t670;
                                GM_ST(void *, (uint8_t *)&t670, (void *)(gm_svqdrhms9i231));
                                GM_ST(uintptr_t, (uint8_t *)&t670 + 8, (uintptr_t)(6));
                                gm_l71 = t670;
                            }
                            goto me637;
mn667:;
ma643:;
                            uint8_t t672 = GM_LD(uint8_t, (((uint8_t *)&t636) + 0));
                            if (t672 != ((uint8_t)35ULL)) goto mn671;
                            void * t673 = GM_LD(void *, ((((uint8_t *)&t636) + 8) + 16));
                            gm_l72 = t673;
                            uint8_t t674 = 0;
                            void * t675 = gm_l72;
                            uint8_t t676;
                            uint8_t t679 = GM_LD(uint8_t, ((((uint8_t *)t675) + 8) + 0));
                            if (t679 != ((uint8_t)32ULL)) goto mn678;
                            t676 = ((uint8_t)1);
                            goto me677;
mn678:;
                            t676 = ((uint8_t)0);
                            goto me677;
mn680:;
                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 191, 38);
me677:;
                            if (!t676) {
                                void * t681 = gm_l72;
                                uint8_t t682;
                                uint8_t t685 = GM_LD(uint8_t, ((((uint8_t *)t681) + 8) + 0));
                                if (t685 != ((uint8_t)33ULL)) goto mn684;
                                t682 = ((uint8_t)1);
                                goto me683;
mn684:;
                                t682 = ((uint8_t)0);
                                goto me683;
mn686:;
                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 191, 66);
me683:;
                                t674 = t682;
                            } else t674 = 1;
                            if (!t674) goto mn671;
                            {
                                gm_b16_8 t688;
                                GM_ST(void *, (uint8_t *)&t688, (void *)(gm_s2737i1iq57nf));
                                GM_ST(uintptr_t, (uint8_t *)&t688 + 8, (uintptr_t)(4));
                                gm_l71 = t688;
                            }
                            goto me637;
mn671:;
ma644:;
                            {
                                uint8_t t692 = GM_LD(uint8_t, (((uint8_t *)&F.l70) + 0));
                                if (t692 != ((uint8_t)2ULL)) goto ie691;
                                gm_t6qooom8qa2do t693 = GM_LD(gm_t6qooom8qa2do, (((uint8_t *)&F.l70) + 8));
                                F.l73 = t693;
                                {
                                    uint8_t t702 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                    switch (t702) { case ((uint8_t)0ULL): goto ma696; case ((uint8_t)1ULL): goto ma696; case ((uint8_t)2ULL): goto ma700; case ((uint8_t)3ULL): goto ma697; case ((uint8_t)4ULL): goto ma697; case ((uint8_t)5ULL): goto ma700; case ((uint8_t)6ULL): goto ma700; case ((uint8_t)7ULL): goto ma700; case ((uint8_t)8ULL): goto ma700; case ((uint8_t)9ULL): goto ma699; case ((uint8_t)10ULL): goto ma698; case ((uint8_t)11ULL): goto ma700; default: goto ma700; }
ma696:;
                                    uint8_t t706 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                    if (t706 != ((uint8_t)0ULL)) goto pa705;
                                    goto po704;
pa705:;
                                    uint8_t t708 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                    if (t708 != ((uint8_t)1ULL)) goto pa707;
                                    goto po704;
pa707:;
                                    goto mn703;
po704:;
                                    {
                                        gm_b16_8 t710;
                                        GM_ST(void *, (uint8_t *)&t710, (void *)(gm_skr6p9lfc5ftp));
                                        GM_ST(uintptr_t, (uint8_t *)&t710 + 8, (uintptr_t)(10));
                                        gm_l71 = t710;
                                    }
                                    goto me695;
mn703:;
ma697:;
                                    uint8_t t714 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                    if (t714 != ((uint8_t)4ULL)) goto pa713;
                                    goto po712;
pa713:;
                                    uint8_t t716 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                    if (t716 != ((uint8_t)3ULL)) goto pa715;
                                    goto po712;
pa715:;
                                    goto mn711;
po712:;
                                    {
                                        gm_b16_8 t718;
                                        GM_ST(void *, (uint8_t *)&t718, (void *)(gm_svqdrhms9i231));
                                        GM_ST(uintptr_t, (uint8_t *)&t718 + 8, (uintptr_t)(6));
                                        gm_l71 = t718;
                                    }
                                    goto me695;
mn711:;
ma698:;
                                    uint8_t t720 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                    if (t720 != ((uint8_t)10ULL)) goto mn719;
                                    {
                                        gm_b16_8 t722;
                                        GM_ST(void *, (uint8_t *)&t722, (void *)(gm_sptnm8c6c52dr));
                                        GM_ST(uintptr_t, (uint8_t *)&t722 + 8, (uintptr_t)(6));
                                        gm_l71 = t722;
                                    }
                                    goto me695;
mn719:;
ma699:;
                                    uint8_t t724 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                    if (t724 != ((uint8_t)9ULL)) goto mn723;
                                    {
                                        gm_b16_8 t726;
                                        GM_ST(void *, (uint8_t *)&t726, (void *)(gm_sg3kpa3h16no3));
                                        GM_ST(uintptr_t, (uint8_t *)&t726 + 8, (uintptr_t)(6));
                                        gm_l71 = t726;
                                    }
                                    goto me695;
mn723:;
ma700:;
                                    goto me695;
mn727:;
ma701:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 194, 21);
me695:;
                                }
                                goto ix691;
ie691:;
ix691:;
                            }
                            goto me637;
mn689:;
ma645:;
                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 185, 9);
me637:;
                        }
                        {
                            void * t729 = gm_l0;
                            void * t730 = (void *)(((uint8_t *)t729) + 208);
                            gm_tt384v6n8ojib t731;
                            void * t732 = gm_l62;
                            gm_b16_8 t733 = GM_LD(gm_b16_8, ((((uint8_t *)t732) + 0) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t731 + 0, t733);
                            void * t734 = gm_l62;
                            gm_tl874t2atnlh3 t735 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t734) + 0) + 16));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t731 + 16, t735);
                            void * t736 = gm_l62;
                            gm_tl874t2atnlh3 t737 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t736) + 40) + 4));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t731 + 28, t737);
                            gm_b16_8 t738 = gm_l71;
                            GM_ST(gm_b16_8, (uint8_t *)&t731 + 40, t738);
                            gm_tj5f8vajf35o7 t739;
                            GM_ST(uint8_t, (uint8_t *)&t739 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t739 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t739 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t739 + 64, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t739 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_tt384v6n8ojib, ((uint8_t *)&t739 + 8), t731);
                            gm_tftb1odc739t5 t740 = gm_f_push_rqlbv1e(t730, &t739);
                            if (((((uint8_t *)&t740))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t740) + 1, gm_srco9vbah234t6e, 204, 9);
                            gm_b0_1 t741 = GM_LD(gm_b0_1, (((uint8_t *)&t740) + 1));
                        }
                        goto ix588;
ie588:;
                        {
                            goto lc512;
                        }
ix588:;
                    }
                    goto me519;
mn520:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 169, 5);
me519:;
                }
lc512:;
                {
                    uint64_t t744 = gm_l60;
                    uint64_t r745; bool o746 = __builtin_add_overflow(t744, ((uint64_t)1ULL), &r745);
                    uint64_t t747 = r745;
                    gm_l60 = t747;
                }
            }
gm_ll516:;
            if (gm_unw.frame == &F.h && gm_unw.label == 10) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb512; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc512; }
            }
            goto gm_lad;
lb512:;
        }
    }
    {
        gm_f_list_1aqn740((void *)&F.l74, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t749 = gm_l0;
            gm_b16_8 t750 = GM_LD(gm_b16_8, ((((uint8_t *)t749) + 208) + 0));
            gm_l75 = t750;
        }
        {
            gm_l76 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t752 = gm_l76;
                uint64_t t753 = GM_LD(uintptr_t, ((uint8_t *)&gm_l75) + 8);
                uint8_t t754 = (t752 < t753);
                if (!t754) goto lb751;
                {
                    uint64_t t757 = gm_l76;
                    uint64_t t758 = (uintptr_t)t757;
                    uint64_t t759 = GM_LD(uintptr_t, ((uint8_t *)&gm_l75) + 8);
                    gm_tj5f8vajf35o7 t762 = GM_LD(gm_tj5f8vajf35o7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l75)) + t758 * 72));
                    F.l77 = t762;
                    {
                        uint8_t t764;
                        uint8_t t771 = GM_LD(uint8_t, (((uint8_t *)&F.l77) + 0));
                        switch (t771) { case ((uint8_t)0ULL): goto ma769; case ((uint8_t)1ULL): goto ma769; case ((uint8_t)2ULL): goto ma769; case ((uint8_t)3ULL): goto ma766; case ((uint8_t)4ULL): goto ma767; case ((uint8_t)5ULL): goto ma768; case ((uint8_t)6ULL): goto ma769; case ((uint8_t)7ULL): goto ma769; case ((uint8_t)8ULL): goto ma769; case ((uint8_t)9ULL): goto ma769; case ((uint8_t)10ULL): goto ma769; default: goto ma769; }
ma766:;
                        uint8_t t773 = GM_LD(uint8_t, (((uint8_t *)&F.l77) + 0));
                        if (t773 != ((uint8_t)3ULL)) goto mn772;
                        gm_tl874t2atnlh3 t774 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l77) + 8) + 0));
                        gm_l78 = t774;
                        gm_closure t775 = gm_l3;
                        gm_tl874t2atnlh3 t776 = gm_l78;
                        gm_closure t777 = t775;
                        gm_node *ccx778 = t777.cx;
                        void * t779 = t777.code;
                        uint8_t t780;
                        if (GM_PTR_TAGGED(t779)) {
                            t780 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t779))(GM_CONV(gm_cseahvg8v7p0vs, t776));
                        } else {
                            t780 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t779)(t777.env, ccx778, NULL, t776);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll755;
                        }
                        t764 = t780;
                        goto me765;
mn772:;
ma767:;
                        uint8_t t782 = GM_LD(uint8_t, (((uint8_t *)&F.l77) + 0));
                        if (t782 != ((uint8_t)4ULL)) goto mn781;
                        gm_tl874t2atnlh3 t783 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l77) + 8) + 0));
                        gm_l79 = t783;
                        gm_closure t784 = gm_l3;
                        gm_tl874t2atnlh3 t785 = gm_l79;
                        gm_closure t786 = t784;
                        gm_node *ccx787 = t786.cx;
                        void * t788 = t786.code;
                        uint8_t t789;
                        if (GM_PTR_TAGGED(t788)) {
                            t789 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t788))(GM_CONV(gm_cseahvg8v7p0vs, t785));
                        } else {
                            t789 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t788)(t786.env, ccx787, NULL, t785);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll755;
                        }
                        t764 = t789;
                        goto me765;
mn781:;
ma768:;
                        uint8_t t791 = GM_LD(uint8_t, (((uint8_t *)&F.l77) + 0));
                        if (t791 != ((uint8_t)5ULL)) goto mn790;
                        gm_tl874t2atnlh3 t792 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l77) + 8));
                        gm_l80 = t792;
                        gm_closure t793 = gm_l3;
                        gm_tl874t2atnlh3 t794 = gm_l80;
                        gm_closure t795 = t793;
                        gm_node *ccx796 = t795.cx;
                        void * t797 = t795.code;
                        uint8_t t798;
                        if (GM_PTR_TAGGED(t797)) {
                            t798 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t797))(GM_CONV(gm_cseahvg8v7p0vs, t794));
                        } else {
                            t798 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t797)(t795.env, ccx796, NULL, t794);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll755;
                        }
                        t764 = t798;
                        goto me765;
mn790:;
ma769:;
                        t764 = ((uint8_t)1);
                        goto me765;
mn799:;
ma770:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 208, 17);
me765:;
                        gm_l81 = t764;
                    }
                    {
                        uint8_t t801 = gm_l81;
                        if (!t801) goto ie800;
                        {
                            void * t803 = (void *)((uint8_t *)&F.l74);
                            gm_tftb1odc739t5 t804 = gm_f_push_rqlbv1e(t803, &(*(gm_tj5f8vajf35o7 *)((uint8_t *)&F.l77)));
                            if (((((uint8_t *)&t804))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t804) + 1, gm_srco9vbah234t6e, 214, 19);
                            gm_b0_1 t805 = GM_LD(gm_b0_1, (((uint8_t *)&t804) + 1));
                        }
                        goto ix800;
ie800:;
ix800:;
                    }
                    goto me760;
mn761:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 207, 5);
me760:;
                }
lc751:;
                {
                    uint64_t t807 = gm_l76;
                    uint64_t r808; bool o809 = __builtin_add_overflow(t807, ((uint64_t)1ULL), &r808);
                    uint64_t t810 = r808;
                    gm_l76 = t810;
                }
            }
gm_ll755:;
            if (gm_unw.frame == &F.h && gm_unw.label == 12) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb751; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc751; }
            }
            goto gm_lad;
lb751:;
        }
    }
    {
        void * t811 = gm_l0;
        gm_trqfvmjtt4sij t812 = F.l74;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t811) + 208), t812);
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
inline __attribute__((aligned(4))) uint8_t gm_f_same_value_7pehjnc(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_value_7pehjnc F;
    void * gm_l0; /* c */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* t */
    uint8_t gm_l5; /* plain */
    GM_STACK_CHECK(&F, 221u);
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
        void * t5 = gm_l1;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 0));
        gm_l4 = t6;
    }
    {
        void * t7 = gm_l3;
        gm_t88rcucg1b7uk t8 = gm_l4;
        uint8_t t9 = gm_f_is_integer_8d4f9ek(t7, GM_CONV(gm_csguankko4lmhd, t8));
        gm_l5 = t9;
    }
    {
        void * t10 = gm_l3;
        gm_t88rcucg1b7uk t11 = gm_l4;
        gm_t53at80pfdus3 t12;
        gm_f_kind_jcnt4hm((void *)&t12, t10, GM_CONV(gm_csguankko4lmhd, t11));
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t17 != ((uint8_t)4ULL)) goto pa16;
        goto po15;
pa16:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t19 != ((uint8_t)8ULL)) goto pa18;
        goto po15;
pa18:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t21 != ((uint8_t)9ULL)) goto pa20;
        goto po15;
pa20:;
        goto mn14;
po15:;
        {
            gm_l5 = ((uint8_t)1);
        }
        goto me13;
mn14:;
        goto me13;
mn23:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 396, 5);
me13:;
    }
    {
        uint8_t t25 = 0;
        uint8_t t26 = 0;
        uint8_t t27 = gm_l5;
        if (t27) {
            void * t28 = gm_l1;
            gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t28) + 72));
            void * t30 = gm_l2;
            gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t30) + 72));
            uint8_t t32 = (!gm_eqff85ac10o3(((uint8_t *)&t29), ((uint8_t *)&t31)));
            t26 = t32;
        } else t26 = 0;
        if (t26) {
            void * t33 = gm_l1;
            void * t34 = gm_l2;
            uint8_t t35 = gm_f_path_eq_l3qbb7v(t33, t34);
            t25 = t35;
        } else t25 = 0;
        uint8_t t36 = 0;
        F.ret = t25;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_use_of_q9agaul(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tcc1rq4n1avb2 a1) {
    struct gm_fr_use_of_q9agaul F;
    void * gm_l0; /* c */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 16);
        gm_tcc1rq4n1avb2 t5 = gm_l1;
        void * t6 = gm_f_get_ptr_1a4voen(t4, t5);
        uint8_t t7;
        if ((GM_LD(void *, ((uint8_t *)&t6)) != 0)) goto mn9;
        t7 = ((uint8_t)1);
        goto me8;
mn9:;
        t7 = ((uint8_t)0);
        goto me8;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 276, 8);
me8:;
        if (!t7) goto ie2;
        {
            void * t12 = gm_l0;
            void * t13 = (void *)(((uint8_t *)t12) + 16);
            gm_tcc1rq4n1avb2 t14 = gm_l1;
            gm_tqetrgo0q24b5 t15;
            memset((uint8_t *)&t15 + 41, 0, 7);
            gm_f_list_7s4om10((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t15 + 0)), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&t15 + 40, ((uint8_t)0));
            gm_tftb1odc739t5 t16 = gm_f_put_he2q4rq(t13, t14, &t15);
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_srco9vbah234t6e, 276, 36);
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t18 = gm_l0;
        void * t19 = (void *)(((uint8_t *)t18) + 16);
        gm_tcc1rq4n1avb2 t20 = gm_l1;
        void * t21 = gm_f_get_ptr_1a4voen(t19, t20);
        if (!(GM_LD(void *, ((uint8_t *)&t21)) != 0)) gm_trap("unwrap of null", gm_srco9vbah234t6e, 277, 12);
        void * t22 = GM_LD(void *, ((uint8_t *)&t21));
        uint8_t t23 = 0;
        F.ret = t22;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_visit_unu9ms6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3) {
    struct gm_fr_visit_unu9ms6 F;
    void * gm_l2; /* e */
    void * gm_l4; /* tys */
    void * gm_l5; /* p */
    uint8_t gm_l6; /* is_const */
    gm_tao63pk3r18v5 gm_l7; /* sl */
    uint8_t gm_l8; /* is_const */
    uint32_t gm_l10; /* g */
    uint32_t gm_l11; /* g */
    uint32_t gm_l12; /* g */
    void * gm_l13; /* b */
    gm_b16_8 gm_l14; /* arms */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l17; /* a */
    gm_trl2khmuphncd gm_l18; /* x */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_tlr9dp305htm7 gm_l21; /* cp */
    void * gm_l22; /* pat */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_tlr9dp305htm7 gm_l26; /* cp */
    void * gm_l27; /* pat */
    gm_t3dcoc7077t1q gm_l28; /* x */
    uint32_t gm_l29; /* l */
    void * gm_l30; /* lhs */
    void * gm_l31; /* rhs */
    uint8_t gm_l32; /* negated */
    gm_tqs5p4n2tq19v gm_l33; /* lb */
    gm_tqs5p4n2tq19v gm_l34; /* rb */
    uint8_t gm_l35; /* b */
    void * gm_l36; /* other */
    gm_ts6oqppjh0ba3 gm_l37; /* op */
    void * gm_l38; /* lhs */
    void * gm_l39; /* rhs */
    GM_STACK_CHECK(&F, 2362u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    F.l3 = a3;
    {
        void * t2 = F.l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l2;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        switch (t20) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma18; case ((uint8_t)2ULL): goto ma18; case ((uint8_t)3ULL): goto ma18; case ((uint8_t)4ULL): goto ma18; case ((uint8_t)5ULL): goto ma18; case ((uint8_t)6ULL): goto ma18; case ((uint8_t)7ULL): goto ma18; case ((uint8_t)8ULL): goto ma18; case ((uint8_t)9ULL): goto ma18; case ((uint8_t)10ULL): goto ma18; case ((uint8_t)11ULL): goto ma18; case ((uint8_t)12ULL): goto ma18; case ((uint8_t)13ULL): goto ma18; case ((uint8_t)14ULL): goto ma18; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma18; case ((uint8_t)17ULL): goto ma18; case ((uint8_t)18ULL): goto ma18; case ((uint8_t)19ULL): goto ma18; case ((uint8_t)20ULL): goto ma18; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma18; case ((uint8_t)23ULL): goto ma18; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma18; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma18; case ((uint8_t)29ULL): goto ma18; case ((uint8_t)30ULL): goto ma17; case ((uint8_t)31ULL): goto ma18; case ((uint8_t)32ULL): goto ma18; case ((uint8_t)33ULL): goto ma18; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma18; case ((uint8_t)36ULL): goto ma18; case ((uint8_t)37ULL): goto ma18; case ((uint8_t)38ULL): goto ma18; case ((uint8_t)39ULL): goto ma18; case ((uint8_t)40ULL): goto ma18; case ((uint8_t)41ULL): goto ma15; case ((uint8_t)42ULL): goto ma18; case ((uint8_t)43ULL): goto ma16; case ((uint8_t)44ULL): goto ma12; case ((uint8_t)45ULL): goto ma18; case ((uint8_t)46ULL): goto ma11; case ((uint8_t)47ULL): goto ma13; case ((uint8_t)48ULL): goto ma14; case ((uint8_t)49ULL): goto ma18; case ((uint8_t)50ULL): goto ma18; case ((uint8_t)51ULL): goto ma18; case ((uint8_t)52ULL): goto ma18; case ((uint8_t)53ULL): goto ma18; case ((uint8_t)54ULL): goto ma18; case ((uint8_t)55ULL): goto ma18; case ((uint8_t)56ULL): goto ma18; case ((uint8_t)57ULL): goto ma18; case ((uint8_t)58ULL): goto ma18; default: goto ma18; }
ma7:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t22 != ((uint8_t)24ULL)) goto mn21;
        void * t23 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
        gm_l5 = t23;
        void * t25 = gm_l4;
        void * t26 = gm_l2;
        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t26) + 0));
        gm_t53at80pfdus3 t28;
        gm_f_kind_jcnt4hm((void *)&t28, t25, GM_CONV(gm_csguankko4lmhd, t27));
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
        if (t29 != ((uint8_t)8ULL)) goto ie24;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)&t28) + 8) + 0));
        gm_l6 = t30;
        {
            uint8_t t33 = gm_l6;
            uint8_t t34 = (uint8_t)!t33;
            if (!t34) goto ie32;
            {
                void * t36 = F.l0;
                uint32_t t37 = F.l1;
                void * t38 = gm_l5;
                gm_b0_1 t39 = gm_f_mutate_oi1s4vs((void *)NULL, cx, NULL, t36, t37, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix32;
ie32:;
ix32:;
        }
        goto ix24;
ie24:;
ix24:;
        goto me6;
mn21:;
ma8:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t41 != ((uint8_t)21ULL)) goto mn40;
        gm_tao63pk3r18v5 t42 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t5) + 8) + 8));
        gm_l7 = t42;
        void * t44 = gm_l4;
        void * t45 = gm_l2;
        gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t45) + 0));
        gm_t53at80pfdus3 t47;
        gm_f_kind_jcnt4hm((void *)&t47, t44, GM_CONV(gm_csguankko4lmhd, t46));
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t47) + 0));
        if (t48 != ((uint8_t)10ULL)) goto ie43;
        uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)&t47) + 8) + 0));
        gm_l8 = t49;
        {
            uint8_t t52 = gm_l8;
            uint8_t t53 = (uint8_t)!t52;
            if (!t53) goto ie51;
            {
                void * t55 = F.l0;
                uint32_t t56 = F.l1;
                void * t57 = GM_LD(void *, (((uint8_t *)&gm_l7) + 0));
                gm_b0_1 t58 = gm_f_mutate_oi1s4vs((void *)NULL, cx, NULL, t55, t56, t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix51;
ie51:;
ix51:;
        }
        goto ix43;
ie43:;
ix43:;
        goto me6;
mn40:;
ma9:;
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t60 != ((uint8_t)34ULL)) goto mn59;
        gm_tsth9qu1dv0g3 t61 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t5) + 8) + 8) + 0));
        F.l9 = t61;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
        if (t63 != ((uint8_t)0ULL)) goto ie62;
        uint32_t t64 = GM_LD(uint32_t, (((uint8_t *)&F.l9) + 8));
        gm_l10 = t64;
        {
            void * t66 = F.l3;
            uint32_t t67 = gm_l10;
            gm_tftb1odc739t5 t68 = gm_f_push_im9d4ro(t66, t67);
            if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_srco9vbah234t6e, 291, 51);
            gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
        }
        goto ix62;
ie62:;
ix62:;
        goto me6;
mn59:;
ma10:;
        uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t73 != ((uint8_t)26ULL)) goto pa72;
        uint32_t t74 = GM_LD(uint32_t, ((((uint8_t *)t5) + 8) + 8));
        gm_l11 = t74;
        goto po71;
pa72:;
        uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t76 != ((uint8_t)27ULL)) goto pa75;
        uint32_t t77 = GM_LD(uint32_t, ((((uint8_t *)t5) + 8) + 8));
        gm_l11 = t77;
        goto po71;
pa75:;
        goto mn70;
po71:;
        {
            void * t79 = F.l3;
            uint32_t t80 = gm_l11;
            gm_tftb1odc739t5 t81 = gm_f_push_im9d4ro(t79, t80);
            if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srco9vbah234t6e, 293, 13);
            gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
        }
        {
            void * t83 = F.l0;
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((((uint8_t *)t83) + 112) + 0));
            gm_b16_8 t85 = t84;
            void * t86 = F.l0;
            void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
            uint32_t t88 = gm_l11;
            uint32_t t89 = gm_f_generic_of_1861hm9(t87, t88);
            uint64_t t90 = (uint64_t)t89;
            uint64_t t91 = (uintptr_t)t90;
            uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&t85) + 8);
            if (t91 >= (uintptr_t)(t92)) gm_trap_index(t91, t92, gm_srco9vbah234t6e, 294, 13);
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t85)) + t91 * 1), ((uint8_t)1));
        }
        goto me6;
mn70:;
ma11:;
        uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t94 != ((uint8_t)46ULL)) goto mn93;
        void * t95 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
        gm_l13 = t95;
        void * t96 = F.l0;
        uint32_t t97 = F.l1;
        void * t98 = gm_l13;
        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)t98) + 0));
        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
        gm_b0_1 t101 = gm_f_block_stmts_c2nnk6m((void *)NULL, cx, NULL, t96, t97, t100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn93:;
ma12:;
        uint8_t t103 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t103 != ((uint8_t)44ULL)) goto mn102;
        gm_b16_8 t104 = GM_LD(gm_b16_8, (((((uint8_t *)t5) + 8) + 8) + 8));
        gm_l14 = t104;
        {
            {
                gm_b16_8 t107 = gm_l14;
                gm_l15 = t107;
            }
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t109 = gm_l16;
                    uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t111 = (t109 < t110);
                    if (!t111) goto lb108;
                    {
                        uint64_t t114 = gm_l16;
                        uint64_t t115 = (uintptr_t)t114;
                        uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        gm_tbbqefuhv2r8h t119 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t115 * 24));
                        gm_l17 = t119;
                        {
                            void * t121 = F.l0;
                            uint32_t t122 = F.l1;
                            void * t123 = GM_LD(void *, (((uint8_t *)&gm_l17) + 0));
                            gm_b0_1 t124 = gm_f_binds_busqeo0((void *)NULL, cx, NULL, t121, t122, t123);
                            if (gm_unw.kind) {
                                goto gm_ll112;
                            }
                        }
                        goto me117;
mn118:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 298, 13);
me117:;
                    }
lc108:;
                    {
                        uint64_t t126 = gm_l16;
                        uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                        uint64_t t129 = r127;
                        gm_l16 = t129;
                    }
                }
gm_ll112:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb108; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc108; }
                }
                goto gm_lad;
lb108:;
            }
        }
        {
            uint32_t t131 = F.l1;
            uint64_t t132 = (uint64_t)t131;
            void * t133 = F.l0;
            void * t134 = GM_LD(void *, (((uint8_t *)t133) + 0));
            uint64_t t135 = GM_LD(uintptr_t, ((((uint8_t *)t134) + 384) + 0) + 8);
            uint8_t t136 = (t132 < t135);
            if (!t136) goto ie130;
            {
                uint32_t t138 = F.l1;
                uint64_t t139 = (uint64_t)t138;
                uint64_t t140 = (uintptr_t)t139;
                void * t141 = F.l0;
                void * t142 = GM_LD(void *, (((uint8_t *)t141) + 0));
                uint64_t t143 = GM_LD(uintptr_t, ((((uint8_t *)t142) + 384) + 0) + 8);
                if (t140 >= (uintptr_t)(t143)) gm_trap_index(t140, t143, gm_srco9vbah234t6e, 299, 71);
                void * t144 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t142) + 384) + 0)) + t140 * 64);
                gm_b16_8 t145 = gm_l14;
                gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&t145));
                void * t147 = F.l0;
                void * t148 = (void *)(((uint8_t *)t147) + 208);
                gm_b0_1 t149 = gm_f_present_only_bindings_ug2i6dq((void *)NULL, cx, NULL, t144, t146, t148);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix130;
ie130:;
ix130:;
        }
        goto me6;
mn102:;
ma13:;
        uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t151 != ((uint8_t)47ULL)) goto mn150;
        gm_trl2khmuphncd t152 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t5) + 8) + 8));
        gm_l18 = t152;
        {
            {
                gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                gm_l19 = t155;
            }
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t157 = gm_l20;
                    uint64_t t158 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    uint8_t t159 = (t157 < t158);
                    if (!t159) goto lb156;
                    {
                        uint64_t t162 = gm_l20;
                        uint64_t t163 = (uintptr_t)t162;
                        uint64_t t164 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        gm_tlr9dp305htm7 t167 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t163 * 24));
                        gm_l21 = t167;
                        {
                            uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                            if (t170 != ((uint8_t)1ULL)) goto ie169;
                            void * t171 = GM_LD(void *, ((((uint8_t *)&gm_l21) + 8) + 0));
                            gm_l22 = t171;
                            {
                                void * t173 = F.l0;
                                uint32_t t174 = F.l1;
                                void * t175 = gm_l22;
                                gm_b0_1 t176 = gm_f_binds_busqeo0((void *)NULL, cx, NULL, t173, t174, t175);
                                if (gm_unw.kind) {
                                    goto gm_ll160;
                                }
                            }
                            goto ix169;
ie169:;
ix169:;
                        }
                        goto me165;
mn166:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 302, 13);
me165:;
                    }
lc156:;
                    {
                        uint64_t t178 = gm_l20;
                        uint64_t r179; bool o180 = __builtin_add_overflow(t178, ((uint64_t)1ULL), &r179);
                        uint64_t t181 = r179;
                        gm_l20 = t181;
                    }
                }
gm_ll160:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb156; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc156; }
                }
                goto gm_lad;
lb156:;
            }
        }
        goto me6;
mn150:;
ma14:;
        uint8_t t183 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t183 != ((uint8_t)48ULL)) goto mn182;
        gm_t710lsrq1m8gk t184 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t5) + 8) + 8));
        F.l23 = t184;
        {
            {
                gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 8));
                gm_l24 = t187;
            }
            {
                gm_l25 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t189 = gm_l25;
                    uint64_t t190 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                    uint8_t t191 = (t189 < t190);
                    if (!t191) goto lb188;
                    {
                        uint64_t t194 = gm_l25;
                        uint64_t t195 = (uintptr_t)t194;
                        uint64_t t196 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                        gm_tlr9dp305htm7 t199 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t195 * 24));
                        gm_l26 = t199;
                        {
                            uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                            if (t202 != ((uint8_t)1ULL)) goto ie201;
                            void * t203 = GM_LD(void *, ((((uint8_t *)&gm_l26) + 8) + 0));
                            gm_l27 = t203;
                            {
                                void * t205 = F.l0;
                                uint32_t t206 = F.l1;
                                void * t207 = gm_l27;
                                gm_b0_1 t208 = gm_f_binds_busqeo0((void *)NULL, cx, NULL, t205, t206, t207);
                                if (gm_unw.kind) {
                                    goto gm_ll192;
                                }
                            }
                            goto ix201;
ie201:;
ix201:;
                        }
                        goto me197;
mn198:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 307, 13);
me197:;
                    }
lc188:;
                    {
                        uint64_t t210 = gm_l25;
                        uint64_t r211; bool o212 = __builtin_add_overflow(t210, ((uint64_t)1ULL), &r211);
                        uint64_t t213 = r211;
                        gm_l25 = t213;
                    }
                }
gm_ll192:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb188; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc188; }
                }
                goto gm_lad;
lb188:;
            }
        }
        goto me6;
mn182:;
ma15:;
        uint8_t t215 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t215 != ((uint8_t)41ULL)) goto mn214;
        gm_t3dcoc7077t1q t216 = GM_LD(gm_t3dcoc7077t1q, ((((uint8_t *)t5) + 8) + 8));
        gm_l28 = t216;
        if (!(((((uint8_t *)&gm_l28) + 8))[4] != 0)) goto ie217;
        uint32_t t218 = GM_LD(uint32_t, (((uint8_t *)&gm_l28) + 8));
        gm_l29 = t218;
        {
            void * t220 = F.l0;
            void * t221 = (void *)(((uint8_t *)t220) + 152);
            uint32_t t222 = F.l1;
            uint32_t t223 = gm_l29;
            gm_tcc1rq4n1avb2 t224;
            GM_ST(uint32_t, ((uint8_t *)&t224 + 0), t222);
            GM_ST(uint32_t, ((uint8_t *)&t224 + 4), t223);
            gm_tftb1odc739t5 t225 = gm_f_put_3g1tdpr(t221, t224, ((uint8_t)1));
            if (((((uint8_t *)&t225))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t225) + 1, gm_srco9vbah234t6e, 311, 41);
            gm_b0_1 t226 = GM_LD(gm_b0_1, (((uint8_t *)&t225) + 1));
        }
        goto ix217;
ie217:;
ix217:;
        goto me6;
mn214:;
ma16:;
        uint8_t t228 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t228 != ((uint8_t)43ULL)) goto mn227;
        void * t229 = GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 0));
        gm_l30 = t229;
        void * t230 = GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 8));
        gm_l31 = t230;
        uint8_t t231 = GM_LD(uint8_t, (((((uint8_t *)t5) + 8) + 8) + 16));
        gm_l32 = t231;
        {
            void * t233 = gm_l30;
            gm_tqs5p4n2tq19v t234 = gm_f_bool_lit_q44074e(t233);
            gm_l33 = t234;
        }
        {
            void * t235 = gm_l31;
            gm_tqs5p4n2tq19v t236 = gm_f_bool_lit_q44074e(t235);
            gm_l34 = t236;
        }
        {
            uint8_t t238;
            if (((((uint8_t *)&gm_l33))[1] != 0)) goto mn240;
            t238 = ((uint8_t)0);
            goto me239;
mn240:;
            t238 = ((uint8_t)1);
            goto me239;
mn241:;
            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 315, 17);
me239:;
            uint8_t t242;
            if (((((uint8_t *)&gm_l34))[1] != 0)) goto mn244;
            t242 = ((uint8_t)0);
            goto me243;
mn244:;
            t242 = ((uint8_t)1);
            goto me243;
mn245:;
            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 315, 41);
me243:;
            uint8_t t246 = (!(t238 == t242));
            if (!t246) goto ie237;
            {
                gm_tqs5p4n2tq19v t248 = gm_l33;
                uint8_t t249;
                if (((((uint8_t *)&t248))[1] != 0)) {
                    uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)&t248) + 0));
                    t249 = t250;
                } else {
                    gm_tqs5p4n2tq19v t251 = gm_l34;
                    if (!((((uint8_t *)&t251))[1] != 0)) gm_trap("unwrap of null", gm_srco9vbah234t6e, 316, 30);
                    uint8_t t252 = GM_LD(uint8_t, ((uint8_t *)&t251));
                    t249 = t252;
                }
                gm_l35 = t249;
            }
            {
                void * t253;
                uint8_t t255;
                if (((((uint8_t *)&gm_l33))[1] != 0)) goto mn257;
                t255 = ((uint8_t)0);
                goto me256;
mn257:;
                t255 = ((uint8_t)1);
                goto me256;
mn258:;
                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 317, 29);
me256:;
                if (!t255) goto ie254;
                void * t260 = gm_l31;
                void * t261 = t260;
                t253 = t261;
                goto ix254;
ie254:;
                void * t263 = gm_l30;
                void * t264 = t263;
                t253 = t264;
ix254:;
                gm_l36 = t253;
            }
            {
                void * t266 = gm_l4;
                void * t267 = gm_l36;
                gm_t88rcucg1b7uk t268 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t267) + 0));
                gm_t53at80pfdus3 t269;
                gm_f_kind_jcnt4hm((void *)&t269, t266, GM_CONV(gm_csguankko4lmhd, t268));
                uint8_t t270 = ((GM_LD(uint8_t, ((uint8_t *)&t269) + 0) == ((uint8_t)4ULL)));
                if (!t270) goto ie265;
                void * t271 = gm_l2;
                gm_tl874t2atnlh3 t272 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t271) + 72));
                gm_tl874t2atnlh3 t273 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                uint8_t t274 = (!gm_eqff85ac10o3(((uint8_t *)&t272), ((uint8_t *)&t273)));
                if (!t274) goto ie265;
                {
                    void * t276 = F.l0;
                    void * t277 = (void *)(((uint8_t *)t276) + 208);
                    gm_tt7dbdq4n1egc t278;
                    memset((uint8_t *)&t278 + 25, 0, 3);
                    void * t279 = gm_l2;
                    gm_tl874t2atnlh3 t280 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t279) + 72));
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t278 + 0, t280);
                    void * t281 = gm_l36;
                    gm_tl874t2atnlh3 t282 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t281) + 72));
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t278 + 12, t282);
                    uint8_t t283 = gm_l35;
                    uint8_t t284 = gm_l32;
                    uint8_t t285 = (!(t283 == t284));
                    GM_ST(uint8_t, (uint8_t *)&t278 + 24, t285);
                    gm_tj5f8vajf35o7 t286;
                    memset(&t286, 0, sizeof t286);
                    GM_ST(uint8_t, ((uint8_t *)&t286 + 0), ((uint8_t)3ULL));
                    GM_ST(gm_tt7dbdq4n1egc, ((uint8_t *)&t286 + 8), t278);
                    gm_tftb1odc739t5 t287 = gm_f_push_rqlbv1e(t277, &t286);
                    if (((((uint8_t *)&t287))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t287) + 1, gm_srco9vbah234t6e, 319, 21);
                    gm_b0_1 t288 = GM_LD(gm_b0_1, (((uint8_t *)&t287) + 1));
                }
                goto ix265;
ie265:;
ix265:;
            }
            goto ix237;
ie237:;
            void * t290 = F.l0;
            void * t291 = gm_l30;
            void * t292 = gm_l31;
            uint8_t t293 = gm_f_same_value_7pehjnc(t290, t291, t292);
            if (!t293) goto ie289;
            {
                void * t295 = F.l0;
                void * t296 = (void *)(((uint8_t *)t295) + 208);
                void * t297 = gm_l2;
                gm_tl874t2atnlh3 t298 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t297) + 72));
                uint8_t t299 = gm_l32;
                uint8_t t300 = (uint8_t)!t299;
                gm_toceffb697p0u t301;
                GM_ST(uint8_t, (uint8_t *)&t301 + 13, 0);
                GM_ST(uint16_t, (uint8_t *)&t301 + 14, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t301 + 0), t298);
                GM_ST(uint8_t, ((uint8_t *)&t301 + 12), t300);
                gm_tj5f8vajf35o7 t302;
                memset(&t302, 0, sizeof t302);
                GM_ST(uint8_t, ((uint8_t *)&t302 + 0), ((uint8_t)4ULL));
                GM_ST(gm_toceffb697p0u, ((uint8_t *)&t302 + 8), t301);
                gm_tftb1odc739t5 t303 = gm_f_push_rqlbv1e(t296, &t302);
                if (((((uint8_t *)&t303))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t303) + 1, gm_srco9vbah234t6e, 322, 17);
                gm_b0_1 t304 = GM_LD(gm_b0_1, (((uint8_t *)&t303) + 1));
            }
            goto ix289;
ie289:;
ix289:;
ix237:;
        }
        goto me6;
mn227:;
ma17:;
        uint8_t t306 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t306 != ((uint8_t)30ULL)) goto mn305;
        gm_ts6oqppjh0ba3 t307 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t5) + 8) + 8) + 0));
        gm_l37 = t307;
        void * t308 = GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 8));
        gm_l38 = t308;
        void * t309 = GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 16));
        gm_l39 = t309;
        {
            gm_ts6oqppjh0ba3 t312 = gm_l37;
            gm_ts6oqppjh0ba3 t313;
            GM_ST(uint8_t, (uint8_t *)&t313 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t313 + 0), ((uint8_t)10ULL));
            gm_ts6oqppjh0ba3 t314;
            GM_ST(uint8_t, (uint8_t *)&t314 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t314 + 0), ((uint8_t)11ULL));
            gm_ts6oqppjh0ba3 t315;
            GM_ST(uint8_t, (uint8_t *)&t315 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t315 + 0), ((uint8_t)12ULL));
            gm_ts6oqppjh0ba3 t316;
            GM_ST(uint8_t, (uint8_t *)&t316 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t316 + 0), ((uint8_t)13ULL));
            gm_t6htphat2tk14 t317;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t317 + 0), t313);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t317 + 2), t314);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t317 + 4), t315);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t317 + 6), t316);
            uint8_t t318 = 0;
            if (gm_eqbqb4gb1d92_2(((uint8_t *)&t317) + 0, ((uint8_t *)&t312))) t318 = 1;
            if (gm_eqbqb4gb1d92_2(((uint8_t *)&t317) + 2, ((uint8_t *)&t312))) t318 = 1;
            if (gm_eqbqb4gb1d92_2(((uint8_t *)&t317) + 4, ((uint8_t *)&t312))) t318 = 1;
            if (gm_eqbqb4gb1d92_2(((uint8_t *)&t317) + 6, ((uint8_t *)&t312))) t318 = 1;
            uint8_t t319 = (uint8_t)(t318);
            if (!t319) goto ie311;
            void * t320 = F.l0;
            void * t321 = gm_l38;
            void * t322 = gm_l39;
            uint8_t t323 = gm_f_same_value_7pehjnc(t320, t321, t322);
            if (!t323) goto ie311;
            {
                void * t325 = F.l0;
                void * t326 = (void *)(((uint8_t *)t325) + 208);
                void * t327 = gm_l2;
                gm_tl874t2atnlh3 t328 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t327) + 72));
                gm_ts6oqppjh0ba3 t329 = gm_l37;
                gm_ts6oqppjh0ba3 t330;
                GM_ST(uint8_t, (uint8_t *)&t330 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t330 + 0), ((uint8_t)11ULL));
                gm_ts6oqppjh0ba3 t331;
                GM_ST(uint8_t, (uint8_t *)&t331 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t331 + 0), ((uint8_t)13ULL));
                gm_t9ldr1q5hn9fq t332;
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t332 + 0), t330);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t332 + 2), t331);
                uint8_t t333 = 0;
                if (gm_eqbqb4gb1d92_2(((uint8_t *)&t332) + 0, ((uint8_t *)&t329))) t333 = 1;
                if (gm_eqbqb4gb1d92_2(((uint8_t *)&t332) + 2, ((uint8_t *)&t329))) t333 = 1;
                uint8_t t334 = (uint8_t)(t333);
                gm_toceffb697p0u t335;
                GM_ST(uint8_t, (uint8_t *)&t335 + 13, 0);
                GM_ST(uint16_t, (uint8_t *)&t335 + 14, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t335 + 0), t328);
                GM_ST(uint8_t, ((uint8_t *)&t335 + 12), t334);
                gm_tj5f8vajf35o7 t336;
                memset(&t336, 0, sizeof t336);
                GM_ST(uint8_t, ((uint8_t *)&t336 + 0), ((uint8_t)4ULL));
                GM_ST(gm_toceffb697p0u, ((uint8_t *)&t336 + 8), t335);
                gm_tftb1odc739t5 t337 = gm_f_push_rqlbv1e(t326, &t336);
                if (((((uint8_t *)&t337))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t337) + 1, gm_srco9vbah234t6e, 327, 17);
                gm_b0_1 t338 = GM_LD(gm_b0_1, (((uint8_t *)&t337) + 1));
            }
            goto ix311;
ie311:;
ix311:;
        }
        goto me6;
mn305:;
ma18:;
        goto me6;
mn339:;
ma19:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 284, 5);
me6:;
    }
    {
        void * t342 = gm_l2;
        uint8_t t343 = GM_LD(uint8_t, ((((uint8_t *)t342) + 8) + 0));
        if (t343 != ((uint8_t)48ULL)) goto ie341;
        gm_t710lsrq1m8gk t344 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t342) + 8) + 8));
        F.l40 = t344;
        {
            void * t346 = F.l0;
            uint32_t t347 = F.l1;
            void * t348 = GM_LD(void *, (((uint8_t *)&F.l40) + 24));
            gm_b16_8 t349 = GM_LD(gm_b16_8, (((uint8_t *)t348) + 0));
            gm_b16_8 t350 = GM_LD(gm_b16_8, ((uint8_t *)&t349));
            gm_b0_1 t351 = gm_f_block_stmts_c2nnk6m((void *)NULL, cx, NULL, t346, t347, t350);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix341;
ie341:;
ix341:;
    }
    {
        void * t352 = gm_l2;
        gm_closure t353 = ((gm_closure){(void *)gm_f_fn_259eng6, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t352, t353);
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
