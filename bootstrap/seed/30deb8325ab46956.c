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
typedef struct gm_csfq64m1m1rt6m { int64_t f0; } gm_csfq64m1m1rt6m;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { int64_t a[4]; } gm_t767tucha9lsh;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csfq64m1m1rt6m) == 8 && _Alignof(gm_csfq64m1m1rt6m) == 8 && offsetof(gm_csfq64m1m1rt6m, f0) == 0, "layout of `Dir`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tm98ap4112hee f0; gm_b16_8 f1; } gm_t3olsle24rveu;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; gm_t9qkh0rsooc8d f8; gm_t9qkh0rsooc8d f9; gm_t9qkh0rsooc8d f10; gm_t9qkh0rsooc8d f11; gm_t9qkh0rsooc8d f12; gm_t9qkh0rsooc8d f13; } gm_t4qfn59m757do;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_t4ce0jd3tdro4 f0; uint8_t f1; uint8_t pe[7]; } gm_t6ie8iajlnpt1;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t767tucha9lsh) == 32 && _Alignof(gm_t767tucha9lsh) == 8 && offsetof(gm_t767tucha9lsh, a) == 0, "layout of `[4]i64`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; } gm_t9rbkbpjtolar;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; uint64_t f1; } gm_te0mgnjdphs16;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tsoiri44d0i6q v1; } f1; } gm_tlmjd3nndlf3r;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_tm98ap4112hee f0; uint8_t f1; uint8_t pe[7]; } gm_tppo2hmut88dq;
typedef struct { gm_tsoiri44d0i6q f0; uint8_t f1; uint8_t pe[3]; } gm_tqnlpelep1582;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; uint16_t v1; } f1; } gm_tvcqs9u9iqpct;
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
typedef struct { gm_t617vr72bd7b3 f0; uint8_t f1; uint8_t pe[7]; } gm_t11t9d8s5diq9;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_th2lqbo77pvsc f7; } gm_t32dckm4jfarl;
_Static_assert(sizeof(gm_t3olsle24rveu) == 24 && _Alignof(gm_t3olsle24rveu) == 8 && offsetof(gm_t3olsle24rveu, f0) == 0 && offsetof(gm_t3olsle24rveu, f1) == 8, "layout of `(file: File, buf: []u8)`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t4qfn59m757do) == 14 && _Alignof(gm_t4qfn59m757do) == 1 && offsetof(gm_t4qfn59m757do, f0) == 0 && offsetof(gm_t4qfn59m757do, f1) == 1 && offsetof(gm_t4qfn59m757do, f2) == 2 && offsetof(gm_t4qfn59m757do, f3) == 3 && offsetof(gm_t4qfn59m757do, f4) == 4 && offsetof(gm_t4qfn59m757do, f5) == 5 && offsetof(gm_t4qfn59m757do, f6) == 6 && offsetof(gm_t4qfn59m757do, f7) == 7 && offsetof(gm_t4qfn59m757do, f8) == 8 && offsetof(gm_t4qfn59m757do, f9) == 9 && offsetof(gm_t4qfn59m757do, f10) == 10 && offsetof(gm_t4qfn59m757do, f11) == 11 && offsetof(gm_t4qfn59m757do, f12) == 12 && offsetof(gm_t4qfn59m757do, f13) == 13, "layout of `(SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp)`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6ie8iajlnpt1) == 16 && _Alignof(gm_t6ie8iajlnpt1) == 8 && offsetof(gm_t6ie8iajlnpt1, f0) == 0 && offsetof(gm_t6ie8iajlnpt1, f1) == 8, "layout of `?Duration`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_trqfvmjtt4sij v1; } f1; } gm_t9922qjstblh1;
_Static_assert(sizeof(gm_t9rbkbpjtolar) == 6 && _Alignof(gm_t9rbkbpjtolar) == 1 && offsetof(gm_t9rbkbpjtolar, f0) == 0 && offsetof(gm_t9rbkbpjtolar, f1) == 1 && offsetof(gm_t9rbkbpjtolar, f2) == 2 && offsetof(gm_t9rbkbpjtolar, f3) == 3 && offsetof(gm_t9rbkbpjtolar, f4) == 4 && offsetof(gm_t9rbkbpjtolar, f5) == 5, "layout of `(Constraint, Constraint, Constraint, Constraint, Constraint, Constraint)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; int64_t v1; } f1; } gm_tarq9lp8tdk6f;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_b16_8 v1; } f1; } gm_td0ept12e7q8b;
_Static_assert(sizeof(gm_te0mgnjdphs16) == 16 && _Alignof(gm_te0mgnjdphs16) == 8 && offsetof(gm_te0mgnjdphs16, f0) == 0 && offsetof(gm_te0mgnjdphs16, f1) == 8, "layout of `Chunk`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_thf5kr0is9u9i f0; gm_tppo2hmut88dq f1; } gm_tfvg1o4f8mmhi;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_te0mgnjdphs16 f0; uint8_t f1; uint8_t pe[7]; } gm_thdrvp9q0u7b5;
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tlmjd3nndlf3r) == 12 && _Alignof(gm_tlmjd3nndlf3r) == 4 && offsetof(gm_tlmjd3nndlf3r, f0) == 0 && offsetof(gm_tlmjd3nndlf3r, f1) == 4, "layout of `IoError!ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t3olsle24rveu v1; gm_tvaas22dl64e0 v2; gm_tm98ap4112hee v3; gm_t3olsle24rveu v4; gm_t3olsle24rveu v5; } f1; } gm_tmvdbo28o1i6h;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t62ebbi2i5rkd v1; } f1; } gm_toqti1ubjnfd1;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tppo2hmut88dq) == 16 && _Alignof(gm_tppo2hmut88dq) == 8 && offsetof(gm_tppo2hmut88dq, f0) == 0 && offsetof(gm_tppo2hmut88dq, f1) == 8, "layout of `?Child`");
_Static_assert(sizeof(gm_tqnlpelep1582) == 12 && _Alignof(gm_tqnlpelep1582) == 4 && offsetof(gm_tqnlpelep1582, f0) == 0 && offsetof(gm_tqnlpelep1582, f1) == 8, "layout of `?ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tqnlpelep1582 v1; } f1; } gm_tr6norheal11n;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
typedef struct { gm_t4c7ipjj9s3fp f0; gm_t4c7ipjj9s3fp f1; gm_t4c7ipjj9s3fp f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; int64_t f5; gm_tf5srh4mp4b3i f6; } gm_ttse10gjbhca2;
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
_Static_assert(sizeof(gm_tvcqs9u9iqpct) == 12 && _Alignof(gm_tvcqs9u9iqpct) == 4 && offsetof(gm_tvcqs9u9iqpct, f0) == 0 && offsetof(gm_tvcqs9u9iqpct, f1) == 4, "layout of `IoError!u16`");
_Static_assert(sizeof(gm_t11t9d8s5diq9) == 24 && _Alignof(gm_t11t9d8s5diq9) == 8 && offsetof(gm_t11t9d8s5diq9, f0) == 0 && offsetof(gm_t11t9d8s5diq9, f1) == 16, "layout of `?DirEntry`");
_Static_assert(sizeof(gm_t32dckm4jfarl) == 80 && _Alignof(gm_t32dckm4jfarl) == 8 && offsetof(gm_t32dckm4jfarl, f0) == 0 && offsetof(gm_t32dckm4jfarl, f1) == 1 && offsetof(gm_t32dckm4jfarl, f2) == 2 && offsetof(gm_t32dckm4jfarl, f3) == 8 && offsetof(gm_t32dckm4jfarl, f4) == 32 && offsetof(gm_t32dckm4jfarl, f5) == 48 && offsetof(gm_t32dckm4jfarl, f6) == 49 && offsetof(gm_t32dckm4jfarl, f7) == 56, "layout of `SpawnOptions`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_tfvg1o4f8mmhi v1; } f1; } gm_t6j8vdmi81h7c;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_t9922qjstblh1) == 48 && _Alignof(gm_t9922qjstblh1) == 8 && offsetof(gm_t9922qjstblh1, f0) == 0 && offsetof(gm_t9922qjstblh1, f1) == 8, "layout of `Flow!List(string)`");
_Static_assert(sizeof(gm_tarq9lp8tdk6f) == 32 && _Alignof(gm_tarq9lp8tdk6f) == 8 && offsetof(gm_tarq9lp8tdk6f, f0) == 0 && offsetof(gm_tarq9lp8tdk6f, f1) == 8, "layout of `Flow!i64`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_td0ept12e7q8b) == 32 && _Alignof(gm_td0ept12e7q8b) == 8 && offsetof(gm_td0ept12e7q8b, f0) == 0 && offsetof(gm_td0ept12e7q8b, f1) == 8, "layout of `Flow!string`");
_Static_assert(sizeof(gm_tfvg1o4f8mmhi) == 32 && _Alignof(gm_tfvg1o4f8mmhi) == 8 && offsetof(gm_tfvg1o4f8mmhi, f0) == 0 && offsetof(gm_tfvg1o4f8mmhi, f1) == 16, "layout of `HostResult`");
_Static_assert(sizeof(gm_thdrvp9q0u7b5) == 24 && _Alignof(gm_thdrvp9q0u7b5) == 8 && offsetof(gm_thdrvp9q0u7b5, f0) == 0 && offsetof(gm_thdrvp9q0u7b5, f1) == 16, "layout of `?Chunk`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t11t9d8s5diq9 v1; } f1; } gm_tiuq7k5hm0kak;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_thdrvp9q0u7b5 v1; } f1; } gm_tlbmih0s72in3;
_Static_assert(sizeof(gm_tmvdbo28o1i6h) == 32 && _Alignof(gm_tmvdbo28o1i6h) == 8 && offsetof(gm_tmvdbo28o1i6h, f0) == 0 && offsetof(gm_tmvdbo28o1i6h, f1) == 8, "layout of `HostOp`");
_Static_assert(sizeof(gm_toqti1ubjnfd1) == 32 && _Alignof(gm_toqti1ubjnfd1) == 8 && offsetof(gm_toqti1ubjnfd1, f0) == 0 && offsetof(gm_toqti1ubjnfd1, f1) == 8, "layout of `Flow!IoError!Socket`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tr6norheal11n) == 16 && _Alignof(gm_tr6norheal11n) == 4 && offsetof(gm_tr6norheal11n, f0) == 0 && offsetof(gm_tr6norheal11n, f1) == 4, "layout of `IoError!?ExitStatus`");
_Static_assert(sizeof(gm_ttse10gjbhca2) == 304 && _Alignof(gm_ttse10gjbhca2) == 8 && offsetof(gm_ttse10gjbhca2, f0) == 0 && offsetof(gm_ttse10gjbhca2, f1) == 56 && offsetof(gm_ttse10gjbhca2, f2) == 112 && offsetof(gm_ttse10gjbhca2, f3) == 168 && offsetof(gm_ttse10gjbhca2, f4) == 224 && offsetof(gm_ttse10gjbhca2, f5) == 280 && offsetof(gm_ttse10gjbhca2, f6) == 288, "layout of `SysState`");
_Static_assert(sizeof(gm_t6j8vdmi81h7c) == 40 && _Alignof(gm_t6j8vdmi81h7c) == 8 && offsetof(gm_t6j8vdmi81h7c, f0) == 0 && offsetof(gm_t6j8vdmi81h7c, f1) == 8, "layout of `Flow!HostResult`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
_Static_assert(sizeof(gm_tiuq7k5hm0kak) == 32 && _Alignof(gm_tiuq7k5hm0kak) == 8 && offsetof(gm_tiuq7k5hm0kak, f0) == 0 && offsetof(gm_tiuq7k5hm0kak, f1) == 8, "layout of `IoError!?DirEntry`");
_Static_assert(sizeof(gm_tlbmih0s72in3) == 32 && _Alignof(gm_tlbmih0s72in3) == 8 && offsetof(gm_tlbmih0s72in3, f0) == 0 && offsetof(gm_tlbmih0s72in3, f1) == 8, "layout of `IoError!?Chunk`");
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cpu_count_0o5882g {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_empty_slice_16ab7e4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_2p7d2kv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_57qa4h8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6l50clp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hgrt2o4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_khi2i98 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_1lff729 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2gkkusq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_aqcn8lk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cijad0r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ducmvdu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_j9i1sr8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mslt7ot {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_r8m0qjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_rg62rr4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tlii61s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_trt04qi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vhnrvmh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_map_3d5nsdr {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_3sndrtb {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_abi2bn7 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_irh77t1 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_ql49boo {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fufbgf7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_mem_trap_ecogko0 {
    gm_frame h;
    gm_tpekhfs32op0f l2; /* e */
    gm_t76123gfet73g ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_socket_on_host_thread_u8c2vg5 {
    gm_frame h;
    gm_toqti1ubjnfd1 ret;
};
struct gm_fr_sys_call_6jqv32o {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* vals */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_sys_code_cjtlfo1 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_count_50c5lcp {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_dest_rtrb14h {
    gm_frame h;
    gm_tpekhfs32op0f l4; /* e */
    gm_td0ept12e7q8b ret;
};
struct gm_fr_sys_fs_u0grkf6 {
    gm_frame h;
    gm_tpekhfs32op0f l28; /* err */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_has_0sego5l {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sys_int_32e79ed {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_kind_sjfdqbt {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_net_aff7h5l {
    gm_frame h;
    gm_b16_8 l6; /* host */
    uint16_t l7; /* port */
    gm_t62ebbi2i5rkd l8; /* r */
    gm_b16_8 l12; /* host */
    uint16_t l13; /* port */
    gm_t62ebbi2i5rkd l14; /* r */
    gm_tm98ap4112hee l18; /* l */
    gm_tvcqs9u9iqpct l19; /* r */
    gm_tm98ap4112hee l22; /* l */
    gm_t62ebbi2i5rkd l23; /* r */
    gm_b16_8 l27; /* data */
    gm_tm98ap4112hee l28; /* s */
    gm_thf5kr0is9u9i l29; /* r */
    gm_b16_8 l30; /* buf */
    gm_tm98ap4112hee l31; /* s */
    gm_thf5kr0is9u9i l32; /* r */
    gm_tm98ap4112hee l33; /* s */
    gm_tm98ap4112hee l34; /* l */
    gm_t9qkh0rsooc8d l35; /* how */
    gm_tm98ap4112hee l36; /* s */
    gm_ttln0d0hd70en l37; /* r */
    gm_tm98ap4112hee l40; /* s */
    gm_t6ie8iajlnpt1 l41; /* recv */
    gm_t6ie8iajlnpt1 l42; /* send */
    gm_ttln0d0hd70en l43; /* r */
    gm_tm98ap4112hee l44; /* s */
    gm_b16_8 l45; /* buf */
    gm_thf5kr0is9u9i l46; /* r */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_net_code_ihgq920 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_net_count_46l7ueq {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_next_handle_r35ek81 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_proc_jhdp277 {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* argv */
    gm_trqfvmjtt4sij l6; /* env */
    gm_t32dckm4jfarl l10; /* opts */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_read_b0gj9fg {
    gm_frame h;
    gm_b16_8 l3; /* buf */
    gm_thf5kr0is9u9i l4; /* r */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_ret_vpa2k8i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sys_run_2vqjo3u {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_sys_src_5svsr6i {
    gm_frame h;
    gm_tpekhfs32op0f l4; /* e */
    gm_td0ept12e7q8b ret;
};
struct gm_fr_sys_state_cvl1lvl {
    gm_frame h;
    gm_ttse10gjbhca2 ret;
};
struct gm_fr_sys_stdio_nsl9o9u {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_sys_strings_r85ragc {
    gm_frame h;
    gm_tpekhfs32op0f l5; /* e */
    gm_trqfvmjtt4sij l7; /* out */
    gm_t9922qjstblh1 ret;
};
struct gm_fr_sys_unit_nrsqciq {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_write_view_pebit6h {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_cpu_count_0o5882g(void);
__attribute__((aligned(4))) void gm_f_defaults_p077jgf(gm_closure a0);
__attribute__((aligned(4))) void gm_f_deinit_gvn47sj(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_tq06jtm(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_16ab7e4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_57qa4h8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6l50clp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hgrt2o4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_khi2i98(void);
__attribute__((aligned(4))) void gm_f_env_exit_prbvpjt(void *gm_out, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_ev_args_j1qj6gl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lff729(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2gkkusq(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqcn8lk(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cijad0r(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ducmvdu(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9i1sr8(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mslt7ot(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r8m0qjr(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rg62rr4(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tlii61s(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_trt04qi(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhnrvmh(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_8o0rtd2(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_f2mbmm8(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_fqooi5f(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_gej4j8d(void * a0, int64_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_v07up09(void * a0, int64_t a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_3d5nsdr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_3sndrtb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_abi2bn7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_irh77t1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_ql49boo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_host_write_r26no87(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_on_host_thread_kmegc07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_perform_native_2ppar83(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_0c6jnkb(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_0tusk8g(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_endcgjj(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_ha6815g(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qkbvor7(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_20jm5n8(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_3j2d0bb(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_7bvuc21(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_82qm8hc(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_jvpr521(void * a0, int64_t a1);
__attribute__((aligned(4))) void gm_f_sleep_gqtie06(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_socket_on_host_thread_u8c2vg5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_sys_call_6jqv32o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) int64_t gm_f_sys_code_cjtlfo1(gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) int64_t gm_f_sys_count_50c5lcp(gm_thf5kr0is9u9i a0);
__attribute__((aligned(4))) void gm_f_sys_dest_rtrb14h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_sys_fs_u0grkf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_sys_has_0sego5l(uint32_t a0, uint32_t a1);
__attribute__((aligned(4))) int64_t gm_f_sys_int_32e79ed(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) int64_t gm_f_sys_kind_sjfdqbt(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_sys_net_aff7h5l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_code_ihgq920(gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_count_46l7ueq(gm_thf5kr0is9u9i a0);
inline __attribute__((aligned(4))) int64_t gm_f_sys_next_handle_r35ek81(void * a0);
__attribute__((aligned(4))) void gm_f_sys_proc_jhdp277(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_sys_read_b0gj9fg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_sys_ret_vpa2k8i(void * a0, gm_b16_8 a1, int64_t a2);
__attribute__((aligned(4))) void gm_f_sys_run_2vqjo3u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_sys_src_5svsr6i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_sys_state_cvl1lvl(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_sys_stdio_nsl9o9u(uint32_t a0);
__attribute__((aligned(4))) void gm_f_sys_strings_r85ragc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) int64_t gm_f_sys_unit_nrsqciq(gm_ttln0d0hd70en a0);
__attribute__((aligned(4))) void gm_f_thread_op_pfli7b1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
extern uint8_t gm_c3qosqjl81k[8] __attribute__((aligned(8)));
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_cbcngu2pqs7[8] __attribute__((aligned(8)));
extern uint8_t gm_cndfnv7mbeh[8] __attribute__((aligned(8)));
extern const uint8_t gm_s27n8348bef64[63];
extern const uint8_t gm_sd8o4m7695hpq[38];
extern const uint8_t gm_sqn7kgtu67hrg[42];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcn3jjc71a2bv3[];
extern const char gm_srcq7ohh8h77bn7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_stfd80s46313k[38];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_q6sor4h F;
    uint64_t gm_l0; /* addr */
    uint64_t gm_l1; /* align */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        uint8_t t4 = (t3 <= ((uint64_t)1ULL));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = gm_l1;
        uint64_t t9 = gm_l1;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 17);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie7;
        {
            uint64_t t16 = gm_l0;
            uint64_t t17 = gm_l1;
            uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
            if (o19) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 50);
            uint64_t t20 = r18;
            uint64_t t21 = (uint64_t)(t16 & t20);
            uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint64_t t24 = gm_l0;
        uint64_t t25 = gm_l1;
        if (t25 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 198, 12);
        uint64_t t26 = (uint64_t)(t24 % t25);
        uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_cpu_count_0o5882g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cpu_count_0o5882g F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint32_t t1 = gm_cpu_count();
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_16ab7e4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_16ab7e4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2p7d2kv F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_57qa4h8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_57qa4h8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6l50clp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6l50clp F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hgrt2o4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hgrt2o4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_khi2i98(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_khi2i98 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lff729(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_1lff729 F;
    GM_STACK_CHECK(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l34));
        gm_node *pn3 = gm_find(cx, 163658498);
        if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 418, 50);
        gm_b0_1 t4;
        t4 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[7])(pn3->env, pn3->hctx, pn3, t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2gkkusq(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_2gkkusq F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l44));
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l45));
        gm_node *pn4 = gm_find(cx, 163658498);
        if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 448, 50);
        gm_thf5kr0is9u9i t5;
        t5 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn4->clauses[10])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l46), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqcn8lk(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_aqcn8lk F;
    GM_STACK_CHECK(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l6));
        uint16_t t3 = GM_LD(uint16_t, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l7));
        gm_node *pn4 = gm_find(cx, 163658498);
        if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 353, 54);
        gm_t62ebbi2i5rkd t5;
        t5 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, uint16_t))pn4->clauses[0])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l8), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cijad0r(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_cijad0r F;
    GM_STACK_CHECK(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l12));
        uint16_t t3 = GM_LD(uint16_t, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l13));
        gm_node *pn4 = gm_find(cx, 163658498);
        if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 364, 50);
        gm_t62ebbi2i5rkd t5;
        t5 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, uint16_t))pn4->clauses[1])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l14), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ducmvdu(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_ducmvdu F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l31));
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l30));
        gm_node *pn4 = gm_find(cx, 163658498);
        if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 408, 54);
        gm_thf5kr0is9u9i t5;
        t5 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn4->clauses[5])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l32), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9i1sr8(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_j9i1sr8 F;
    GM_STACK_CHECK(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l40));
        gm_t6ie8iajlnpt1 t3 = GM_LD(gm_t6ie8iajlnpt1, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l41));
        gm_t6ie8iajlnpt1 t4 = GM_LD(gm_t6ie8iajlnpt1, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l42));
        gm_node *pn5 = gm_find(cx, 163658498);
        if (!pn5) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 441, 50);
        gm_ttln0d0hd70en t6;
        t6 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_t6ie8iajlnpt1, gm_t6ie8iajlnpt1))pn5->clauses[9])(pn5->env, pn5->hctx, pn5, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_ttln0d0hd70en, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l43), t6);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mslt7ot(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_mslt7ot F;
    GM_STACK_CHECK(&F, 20u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l18));
        gm_node *pn3 = gm_find(cx, 163658498);
        if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 373, 50);
        gm_tvcqs9u9iqpct t4;
        t4 = ((gm_tvcqs9u9iqpct (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[2])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_tvcqs9u9iqpct, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l19), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r8m0qjr(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_r8m0qjr F;
    GM_STACK_CHECK(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l33));
        gm_node *pn3 = gm_find(cx, 163658498);
        if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 414, 50);
        gm_b0_1 t4;
        t4 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[6])(pn3->env, pn3->hctx, pn3, t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rg62rr4(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_rg62rr4 F;
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l22));
        gm_node *pn3 = gm_find(cx, 163658498);
        if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 383, 54);
        gm_t62ebbi2i5rkd t4;
        t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[3])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l23), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tlii61s(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_tlii61s F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l28));
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l27));
        gm_node *pn4 = gm_find(cx, 163658498);
        if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 397, 54);
        gm_thf5kr0is9u9i t5;
        t5 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn4->clauses[4])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l29), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_trt04qi(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_trt04qi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_read_b0gj9fg *)((&F.h)->up))->l3));
        gm_node *pn3 = gm_find(cx, 1018230276);
        if (!pn3) gm_trap("no handler installed for effect `In`", gm_srcn3jjc71a2bv3, 207, 41);
        gm_thf5kr0is9u9i t4;
        t4 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[0])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_read_b0gj9fg *)((&F.h)->up))->l4), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhnrvmh(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_vhnrvmh F;
    GM_STACK_CHECK(&F, 21u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l36));
        gm_t9qkh0rsooc8d t3 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l35));
        gm_node *pn4 = gm_find(cx, 163658498);
        if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 431, 50);
        gm_ttln0d0hd70en t5;
        t5 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_t9qkh0rsooc8d))pn4->clauses[8])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_ttln0d0hd70en, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l37), t5);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_3d5nsdr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_3d5nsdr F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_hgrt2o4();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_3sndrtb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_3sndrtb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_57qa4h8();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_abi2bn7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_abi2bn7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_6l50clp();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_irh77t1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_irh77t1 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_16ab7e4();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_ql49boo(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_ql49boo F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_khi2i98();
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
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_mem_trap_ecogko0 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)a2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_u6r4vns F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 566u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = gm_l2;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l3;
        uint8_t t6 = (t5 <= ((uint64_t)1ULL));
        if (!t6) goto ie2;
        {
            gm_b16_8 t8 = gm_f_empty_slice_ijtp9lo();
            gm_t934k1j7u70ct t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
            ((uint8_t *)&t9)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 8), t8);
            gm_t934k1j7u70ct t10 = t9;
            uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t13 = gm_l1;
        uint64_t t14 = gm_l3;
        uint8_t t15 = gm_f_aligned_q6sor4h(t13, t14);
        if (!t15) goto ie12;
        {
            void * t18 = gm_l0;
            uint64_t t19 = gm_l1;
            uint64_t t20 = gm_l2;
            void * t21 = gm_f_view_qalkbv1(t18, t19, t20, ((uint8_t)0));
            void * t22 = t21;
            if (!(GM_LD(void *, ((uint8_t *)&t22)) != 0)) goto ie17;
            void * t23 = GM_LD(void *, ((uint8_t *)&t22));
            gm_l4 = t23;
            {
                void * t25 = gm_l4;
                void * t26 = ((uint8_t *)t25);
                uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t28 = gm_l2;
                uint64_t t29 = gm_f_us_cuv01fp(t28);
                uint64_t t30 = (uintptr_t)t29;
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t26 + t27 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30 - t27));
                gm_t934k1j7u70ct t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 40, 0);
                ((uint8_t *)&t32)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t32 + 8), t31);
                gm_t934k1j7u70ct t33 = t32;
                uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
                F.ret = t32;
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t35 = gm_l0;
        uint64_t t36 = gm_l1;
        uint64_t t37 = gm_l2;
        uint64_t t38 = gm_l3;
        gm_tpekhfs32op0f t39;
        gm_f_fault_u31b92l((void *)&t39, t35, t36, t37, t38, ((uint8_t)0));
        gm_t934k1j7u70ct t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        ((uint8_t *)&t40)[0] = 1;
        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t40 + 8), t39);
        gm_t934k1j7u70ct t41 = t40;
        uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
        F.ret = t40;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) int64_t gm_f_sys_count_50c5lcp(gm_thf5kr0is9u9i a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_count_50c5lcp F;
    gm_thf5kr0is9u9i gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    if (!((((uint8_t *)&gm_l0))[0] != 0)) goto mn3;
    gm_toe5fmr3vvf4d t4 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 8));
    gm_l1 = t4;
    gm_toe5fmr3vvf4d t5 = gm_l1;
    int64_t t6 = gm_f_sys_code_cjtlfo1(t5);
    t1 = t6;
    goto me2;
mn3:;
    if (((((uint8_t *)&gm_l0))[0] != 0)) goto mn7;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l2 = t8;
    uint64_t t9 = gm_l2;
    int64_t t10 = (int64_t)t9;
    t1 = t10;
    goto me2;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 78, 45);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_sys_has_0sego5l(uint32_t a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_has_0sego5l F;
    uint32_t gm_l0; /* flags */
    uint32_t gm_l1; /* bit */
    GM_STACK_CHECK_SHALLOW(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l0;
    uint32_t t2 = gm_l1;
    uint32_t t3 = (uint32_t)(t1 & t2);
    uint8_t t4 = (!(t3 == ((uint32_t)0ULL)));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_kind_sjfdqbt(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_kind_sjfdqbt F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 30u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; default: goto ma7; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)0ULL)) goto mn9;
    t1 = ((int64_t)0LL);
    goto me2;
mn9:;
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)1ULL)) goto mn11;
    t1 = ((int64_t)1LL);
    goto me2;
mn11:;
ma5:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t14 != ((uint8_t)2ULL)) goto mn13;
    t1 = ((int64_t)2LL);
    goto me2;
mn13:;
ma6:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)3ULL)) goto mn15;
    t1 = ((int64_t)3LL);
    goto me2;
mn15:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 96, 42);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_code_ihgq920(gm_toe5fmr3vvf4d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_net_code_ihgq920 F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    uint8_t t3 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l0) + 0) == ((uint8_t)3ULL)));
    if (!t3) goto ie2;
    int64_t t5 = GM_LD(int64_t, gm_cbcngu2pqs7);
    int64_t t6 = t5;
    t1 = t6;
    goto ix2;
ie2:;
    gm_toe5fmr3vvf4d t8 = gm_l0;
    int64_t t9 = gm_f_sys_code_cjtlfo1(t8);
    int64_t t10 = t9;
    t1 = t10;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_count_46l7ueq(gm_thf5kr0is9u9i a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_net_count_46l7ueq F;
    gm_thf5kr0is9u9i gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    if (!((((uint8_t *)&gm_l0))[0] != 0)) goto mn3;
    gm_toe5fmr3vvf4d t4 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 8));
    gm_l1 = t4;
    gm_toe5fmr3vvf4d t5 = gm_l1;
    int64_t t6 = gm_f_sys_net_code_ihgq920(t5);
    t1 = t6;
    goto me2;
mn3:;
    if (((((uint8_t *)&gm_l0))[0] != 0)) goto mn7;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l2 = t8;
    uint64_t t9 = gm_l2;
    int64_t t10 = (int64_t)t9;
    t1 = t10;
    goto me2;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 84, 49);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_next_handle_r35ek81(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_next_handle_r35ek81 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        int64_t t4 = GM_LD(int64_t, (((uint8_t *)t3) + 280));
        int64_t r5; bool o6 = __builtin_add_overflow(t4, ((int64_t)1LL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 50, 5);
        int64_t t7 = r5;
        GM_ST(int64_t, (((uint8_t *)t2) + 280), t7);
    }
    {
        void * t8 = gm_l0;
        int64_t t9 = GM_LD(int64_t, (((uint8_t *)t8) + 280));
        int64_t r10; bool o11 = __builtin_add_overflow(t9, ((int64_t)2LL), &r10);
        if (o11) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 51, 12);
        int64_t t12 = r10;
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sys_ret_vpa2k8i(void * a0, gm_b16_8 a1, int64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_ret_vpa2k8i F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* dst */
    int64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        int64_t t4 = gm_l2;
        int64_t t5 = t4;
        uint64_t t6;
        memset(&t6, 0, sizeof t6); memcpy(&t6, ((uint8_t *)&t5), sizeof t6 < 8 ? sizeof t6 : 8);
        unsigned __int128 t7 = (unsigned __int128)t6;
        gm_f_put_uint_cddin99(t2, t3, t7);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_sys_stdio_nsl9o9u(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_stdio_nsl9o9u F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 26u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t1 = gm_l0;
    uint32_t t2 = t1;
    gm_t9qkh0rsooc8d t3;
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&t2));
    if (!(t6 == ((uint32_t)1ULL))) goto mn5;
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1ULL));
    t3 = t7;
    goto me4;
mn5:;
    uint32_t t9 = GM_LD(uint32_t, ((uint8_t *)&t2));
    if (!(t9 == ((uint32_t)2ULL))) goto mn8;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)2ULL));
    t3 = t10;
    goto me4;
mn8:;
    gm_t9qkh0rsooc8d t12;
    GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
    t3 = t12;
    goto me4;
mn11:;
    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 106, 48);
me4:;
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_unit_nrsqciq(gm_ttln0d0hd70en a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_unit_nrsqciq F;
    gm_ttln0d0hd70en gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    if (!((((uint8_t *)&gm_l0))[0] != 0)) goto mn3;
    gm_toe5fmr3vvf4d t4 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t4;
    gm_toe5fmr3vvf4d t5 = gm_l1;
    int64_t t6 = gm_f_sys_code_cjtlfo1(t5);
    t1 = t6;
    goto me2;
mn3:;
    t1 = ((int64_t)0LL);
    goto me2;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 90, 41);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_qalkbv1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint8_t gm_l3; /* write */
    uint64_t gm_l4; /* p */
    void * gm_l5; /* pg */
    void * gm_l6; /* ss */
    uint64_t gm_l7; /* rel */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* s */
    uint64_t gm_l10; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 569u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l1;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
        uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
        uint64_t t7 = r5;
        uint64_t t8 = GM_LD(uint64_t, gm_c6p4chcutca);
        unsigned __int128 n9 = (unsigned __int128)t8;
        if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 206, 10);
        uint64_t t10 = (uint64_t)(t7 >> (int)n9);
        gm_l4 = t10;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 0) + 0) + 8);
        uint64_t t15 = (uint64_t)t14;
        uint8_t t16 = (t12 >= t15);
        if (!t16) goto ie11;
        {
            uint8_t t18 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((((uint8_t *)t19) + 0) + 0));
        void * t21 = t20;
        uint64_t t22 = gm_l4;
        uint64_t t23 = gm_f_us_cuv01fp(t22);
        uint64_t t24 = (uintptr_t)t23;
        void * t25 = (void *)((uint8_t *)t21 + t24 * 32);
        gm_l5 = t25;
    }
    {
        void * t26 = gm_l5;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
        void * t28 = t27;
        void * t29;
        if ((GM_LD(void *, ((uint8_t *)&t28)) != 0)) {
            void * t30 = GM_LD(void *, (((uint8_t *)&t28) + 0));
            t29 = t30;
        } else {
            uint8_t t31 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l6 = t29;
    }
    {
        uint64_t t32 = gm_l1;
        void * t33 = gm_l5;
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 8));
        uint64_t r35; bool o36 = __builtin_sub_overflow(t32, t34, &r35);
        if (o36) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
        uint64_t t37 = r35;
        gm_l7 = t37;
    }
    {
        uint64_t t38 = gm_l7;
        void * t39 = gm_l5;
        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 16));
        unsigned __int128 n41 = (unsigned __int128)t40;
        if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcq7ohh8h77bn7, 211, 10);
        uint64_t t42 = (uint64_t)(t38 >> (int)n41);
        gm_l8 = t42;
    }
    {
        void * t43 = gm_l6;
        void * t44 = t43;
        uint64_t t45 = gm_l8;
        uint64_t t46 = gm_f_us_cuv01fp(t45);
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = (void *)((uint8_t *)t44 + t47 * 24);
        gm_l9 = t48;
    }
    {
        uint64_t t49 = gm_l7;
        uint64_t t50 = gm_l8;
        void * t51 = gm_l5;
        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t51) + 16));
        unsigned __int128 n53 = (unsigned __int128)t52;
        if (n53 >= 64) gm_trap_shift((uint64_t)n53, "u64", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t r54 = (uint64_t)((uint64_t)t50 << (int)n53);
        bool o55 = (uint64_t)(r54 >> (int)n53) != t50;
        if (o55) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t t56 = r54;
        uint64_t r57; bool o58 = __builtin_sub_overflow(t49, t56, &r57);
        if (o58) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
        uint64_t t59 = r57;
        gm_l10 = t59;
    }
    {
        uint8_t t61 = 0;
        uint8_t t62 = 0;
        uint8_t t63 = 0;
        void * t64 = gm_l9;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)t64) + 17));
        uint8_t t66 = (uint8_t)!t65;
        if (!t66) {
            uint64_t t67 = gm_l2;
            void * t68 = gm_l9;
            uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)t68) + 8));
            uint8_t t70 = (t67 > t69);
            t63 = t70;
        } else t63 = 1;
        if (!t63) {
            uint64_t t71 = gm_l10;
            void * t72 = gm_l9;
            uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)t72) + 8));
            uint64_t t74 = gm_l2;
            uint64_t r75; bool o76 = __builtin_sub_overflow(t73, t74, &r75);
            if (o76) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
            uint64_t t77 = r75;
            uint8_t t78 = (t71 > t77);
            t62 = t78;
        } else t62 = 1;
        if (!t62) {
            uint8_t t79 = 0;
            uint8_t t80 = gm_l3;
            if (t80) {
                void * t81 = gm_l9;
                uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)t81) + 18));
                uint8_t t83 = (uint8_t)!t82;
                t79 = t83;
            } else t79 = 0;
            t61 = t79;
        } else t61 = 1;
        if (!t61) goto ie60;
        {
            uint8_t t85 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix60;
ie60:;
ix60:;
    }
    {
        void * t86 = gm_l9;
        void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
        void * t88 = t87;
        uint64_t t89 = gm_l10;
        uint64_t t90 = gm_f_us_cuv01fp(t89);
        uint64_t t91 = (uintptr_t)t90;
        void * t92 = (void *)((uint8_t *)t88 + t91 * 1);
        void * t93 = t92;
        uint8_t t94 = 0;
        F.ret = t93;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_view_pebit6h F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 566u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = gm_l2;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l3;
        uint8_t t6 = (t5 <= ((uint64_t)1ULL));
        if (!t6) goto ie2;
        {
            gm_b16_8 t8 = gm_f_empty_slice_ijtp9lo();
            gm_t934k1j7u70ct t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
            ((uint8_t *)&t9)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 8), t8);
            gm_t934k1j7u70ct t10 = t9;
            uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t13 = gm_l1;
        uint64_t t14 = gm_l3;
        uint8_t t15 = gm_f_aligned_q6sor4h(t13, t14);
        if (!t15) goto ie12;
        {
            void * t18 = gm_l0;
            uint64_t t19 = gm_l1;
            uint64_t t20 = gm_l2;
            void * t21 = gm_f_view_qalkbv1(t18, t19, t20, ((uint8_t)1));
            void * t22 = t21;
            if (!(GM_LD(void *, ((uint8_t *)&t22)) != 0)) goto ie17;
            void * t23 = GM_LD(void *, ((uint8_t *)&t22));
            gm_l4 = t23;
            {
                void * t25 = gm_l4;
                void * t26 = ((uint8_t *)t25);
                uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t28 = gm_l2;
                uint64_t t29 = gm_f_us_cuv01fp(t28);
                uint64_t t30 = (uintptr_t)t29;
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t26 + t27 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30 - t27));
                gm_t934k1j7u70ct t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t32 + 40, 0);
                ((uint8_t *)&t32)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t32 + 8), t31);
                gm_t934k1j7u70ct t33 = t32;
                uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
                F.ret = t32;
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t35 = gm_l0;
        uint64_t t36 = gm_l1;
        uint64_t t37 = gm_l2;
        uint64_t t38 = gm_l3;
        gm_tpekhfs32op0f t39;
        gm_f_fault_u31b92l((void *)&t39, t35, t36, t37, t38, ((uint8_t)1));
        gm_t934k1j7u70ct t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        ((uint8_t *)&t40)[0] = 1;
        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t40 + 8), t39);
        gm_t934k1j7u70ct t41 = t40;
        uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
        F.ret = t40;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lff729(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2gkkusq(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqcn8lk(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cijad0r(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ducmvdu(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9i1sr8(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mslt7ot(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r8m0qjr(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rg62rr4(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tlii61s(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_trt04qi(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhnrvmh(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_socket_on_host_thread_u8c2vg5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_socket_on_host_thread_u8c2vg5 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tmvdbo28o1i6h gm_l2; /* op */
    gm_tfvg1o4f8mmhi gm_l3; /* r */
    gm_t62ebbi2i5rkd gm_l4; /* result */
    gm_tm98ap4112hee gm_l5; /* s */
    gm_toe5fmr3vvf4d gm_l6; /* e */
    GM_STACK_CHECK(&F, 602u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_t6j8vdmi81h7c t4;
        gm_f_on_host_thread_kmegc07((void *)&t4, (void *)NULL, cx, NULL, t2, t3, &(*(gm_tmvdbo28o1i6h *)((uint8_t *)&gm_l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j8vdmi81h7c t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_t76123gfet73g t6 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t5) + 8));
            gm_toqti1ubjnfd1 t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_tfvg1o4f8mmhi t8 = GM_LD(gm_tfvg1o4f8mmhi, (((uint8_t *)&t5) + 8));
        gm_l3 = t8;
    }
    {
        gm_toe5fmr3vvf4d t9;
        GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)12ULL));
        gm_t62ebbi2i5rkd t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
        ((uint8_t *)&t10)[0] = 1;
        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t10 + 8), t9);
        gm_l4 = t10;
    }
    {
        if (!(((((uint8_t *)&gm_l3) + 16))[8] != 0)) goto ie11;
        gm_tm98ap4112hee t12 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l3) + 16));
        gm_l5 = t12;
        {
            gm_tm98ap4112hee t14 = gm_l5;
            gm_t62ebbi2i5rkd t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t15 + 8), t14);
            gm_l4 = t15;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        {
            gm_thf5kr0is9u9i t17 = GM_LD(gm_thf5kr0is9u9i, (((uint8_t *)&gm_l3) + 0));
            gm_thf5kr0is9u9i t18 = t17;
            uint64_t t19;
            if (!((((uint8_t *)&t18))[0] != 0)) {
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&t18) + 8));
                t19 = t20;
            } else {
                gm_toe5fmr3vvf4d t21 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t18) + 8));
                gm_l6 = t21;
                {
                    gm_toe5fmr3vvf4d t23 = gm_l6;
                    gm_t62ebbi2i5rkd t24;
                    GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                    ((uint8_t *)&t24)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t24 + 8), t23);
                    gm_l4 = t24;
                }
                gm_t62ebbi2i5rkd t25 = gm_l4;
                gm_toqti1ubjnfd1 t26;
                GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 24, 0);
                ((uint8_t *)&t26)[0] = 0;
                GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&t26 + 8), t25);
                gm_toqti1ubjnfd1 t27 = t26;
                uint8_t t28 = ((((uint8_t *)&t27))[0] != 0);
                F.ret = t26;
                goto gm_fret;
            }
        }
    }
    {
        gm_t62ebbi2i5rkd t29 = gm_l4;
        gm_toqti1ubjnfd1 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t30 + 24, 0);
        ((uint8_t *)&t30)[0] = 0;
        GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&t30 + 8), t29);
        gm_toqti1ubjnfd1 t31 = t30;
        uint8_t t32 = ((((uint8_t *)&t31))[0] != 0);
        F.ret = t30;
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
inline __attribute__((aligned(4))) void gm_f_sys_call_6jqv32o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_sys_call_6jqv32o F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    void * gm_l4; /* e */
    gm_b16_8 gm_l5; /* dst */
    gm_t2lqu8jsj511d gm_l6; /* mk */
    gm_tcfa6l7reoosn gm_l8; /* r */
    GM_STACK_CHECK(&F, 689u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        gm_t2lqu8jsj511d t3 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t2));
        gm_l6 = t3;
    }
    {
        gm_f_list_fufbgf7((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = gm_l0;
        gm_tbl8il92l97ut t5 = gm_l1;
        gm_b16_8 t6 = gm_l3;
        void * t7 = (void *)((uint8_t *)&F.l7);
        gm_tcfa6l7reoosn t8;
        gm_f_ev_args_j1qj6gl((void *)&t8, (void *)NULL, cx, NULL, t4, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) {
            gm_t76123gfet73g t10 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t9) + 8));
            gm_tcfa6l7reoosn t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t11 + 8), t10);
            F.ret = t11;
            goto gm_fret;
        }
        gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 8));
    }
    {
        gm_tcfa6l7reoosn t13;
        gm_t9qkh0rsooc8d t15 = gm_l2;
        gm_t9qkh0rsooc8d t16;
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)47ULL));
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)48ULL));
        gm_t9qkh0rsooc8d t18;
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)49ULL));
        gm_t9qkh0rsooc8d t19;
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)50ULL));
        gm_t9qkh0rsooc8d t20;
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)51ULL));
        gm_t9qkh0rsooc8d t21;
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)52ULL));
        gm_t9rbkbpjtolar t22;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 0), t16);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 1), t17);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 2), t18);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 3), t19);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 4), t20);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 5), t21);
        gm_t9rbkbpjtolar t23 = t22;
        uint8_t t24 = 0;
        gm_t9qkh0rsooc8d t25 = t15;
        if (gm_eqtje7cegoml(((uint8_t *)&t23) + 0, ((uint8_t *)&t25))) t24 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t23) + 1, ((uint8_t *)&t25))) t24 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t23) + 2, ((uint8_t *)&t25))) t24 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t23) + 3, ((uint8_t *)&t25))) t24 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t23) + 4, ((uint8_t *)&t25))) t24 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t23) + 5, ((uint8_t *)&t25))) t24 = 1;
        uint8_t t26 = (uint8_t)(t24);
        if (!t26) goto ie14;
        void * t28 = gm_l0;
        void * t29 = gm_l4;
        gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t29) + 112));
        gm_t9qkh0rsooc8d t31 = gm_l2;
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t33 = gm_l5;
        gm_tcfa6l7reoosn t34;
        gm_f_thread_op_pfli7b1((void *)&t34, (void *)NULL, cx, NULL, t28, t30, t31, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t35 = t34;
        t13 = t35;
        goto ix14;
ie14:;
        void * t37 = gm_l0;
        void * t38 = gm_l4;
        gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 112));
        gm_t9qkh0rsooc8d t40 = gm_l2;
        gm_b16_8 t41 = gm_l3;
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t43 = t42;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
        gm_b16_8 t45 = gm_l5;
        gm_tcfa6l7reoosn t46;
        gm_f_sys_run_2vqjo3u((void *)&t46, (void *)NULL, cx, NULL, t37, t39, t40, t41, t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t47 = t46;
        t13 = t47;
ix14:;
        gm_l8 = t13;
    }
    {
        void * t48 = (void *)((uint8_t *)&F.l7);
        gm_f_deinit_tq06jtm(t48);
    }
    {
        void * t49 = gm_l0;
        gm_t2lqu8jsj511d t50 = gm_l6;
        gm_f_release_jn5kqdi(t49, GM_CONV(gm_csc7lc6jlkqkuq, t50));
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l8), 32);
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
inline __attribute__((aligned(4))) int64_t gm_f_sys_code_cjtlfo1(gm_toe5fmr3vvf4d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_code_cjtlfo1 F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    int32_t gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t18) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; default: goto ma17; }
ma3:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t20 != ((uint8_t)0ULL)) goto mn19;
    t1 = ((int64_t)-1LL);
    goto me2;
mn19:;
ma4:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t22 != ((uint8_t)1ULL)) goto mn21;
    t1 = ((int64_t)-2LL);
    goto me2;
mn21:;
ma5:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t24 != ((uint8_t)2ULL)) goto mn23;
    t1 = ((int64_t)-3LL);
    goto me2;
mn23:;
ma6:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t26 != ((uint8_t)3ULL)) goto mn25;
    t1 = ((int64_t)-4LL);
    goto me2;
mn25:;
ma7:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t28 != ((uint8_t)4ULL)) goto mn27;
    t1 = ((int64_t)-5LL);
    goto me2;
mn27:;
ma8:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t30 != ((uint8_t)5ULL)) goto mn29;
    t1 = ((int64_t)-6LL);
    goto me2;
mn29:;
ma9:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t32 != ((uint8_t)6ULL)) goto mn31;
    t1 = ((int64_t)-7LL);
    goto me2;
mn31:;
ma10:;
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t34 != ((uint8_t)7ULL)) goto mn33;
    t1 = ((int64_t)-8LL);
    goto me2;
mn33:;
ma11:;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t36 != ((uint8_t)8ULL)) goto mn35;
    t1 = ((int64_t)-9LL);
    goto me2;
mn35:;
ma12:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t38 != ((uint8_t)9ULL)) goto mn37;
    t1 = ((int64_t)-10LL);
    goto me2;
mn37:;
ma13:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t40 != ((uint8_t)10ULL)) goto mn39;
    t1 = ((int64_t)-11LL);
    goto me2;
mn39:;
ma14:;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t42 != ((uint8_t)11ULL)) goto mn41;
    t1 = ((int64_t)-12LL);
    goto me2;
mn41:;
ma15:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t44 != ((uint8_t)12ULL)) goto mn43;
    t1 = ((int64_t)-13LL);
    goto me2;
mn43:;
ma16:;
    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t46 != ((uint8_t)13ULL)) goto mn45;
    int32_t t47 = GM_LD(int32_t, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t47;
    int32_t t48 = gm_l1;
    int64_t t49 = (int64_t)t48;
    int64_t r50; bool o51 = __builtin_sub_overflow(((int64_t)-1000LL), t49, &r50);
    if (o51) gm_trap("integer overflow in `-`", gm_srcn3jjc71a2bv3, 70, 18);
    int64_t t52 = r50;
    t1 = t52;
    goto me2;
mn45:;
ma17:;
    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 56, 38);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) int64_t gm_f_sys_count_50c5lcp(gm_thf5kr0is9u9i a0);
inline __attribute__((aligned(4))) void gm_f_sys_dest_rtrb14h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_dest_rtrb14h F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* b */
    gm_t2lqu8jsj511d gm_l3; /* sp */
    gm_b16_8 gm_l5; /* v */
    GM_STACK_CHECK(&F, 498u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        gm_t2lqu8jsj511d t4 = gm_f_slice_parts_1se5hu4(t2, t3);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 184);
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        gm_t934k1j7u70ct t9;
        gm_f_write_view_pebit6h((void *)&t9, t6, t7, t8, ((uint64_t)1ULL));
        gm_t934k1j7u70ct t10 = t9;
        gm_b16_8 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
            t11 = t12;
        } else {
            gm_tpekhfs32op0f t13 = GM_LD(gm_tpekhfs32op0f, (((uint8_t *)&t10) + 8));
            F.l4 = t13;
            void * t14 = gm_l0;
            gm_tl874t2atnlh3 t15 = gm_l1;
            gm_t76123gfet73g t16;
            gm_f_mem_trap_ecogko0((void *)&t16, (void *)NULL, cx, NULL, t14, t15, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t17 + 8), t16);
            gm_td0ept12e7q8b t18 = t17;
            uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
            F.ret = t17;
            goto gm_fret;
        }
        gm_l5 = t11;
    }
    {
        gm_b16_8 t20 = gm_l5;
        gm_td0ept12e7q8b t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t21 + 24, 0);
        ((uint8_t *)&t21)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t21 + 8), t20);
        gm_td0ept12e7q8b t22 = t21;
        uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
        F.ret = t21;
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
__attribute__((aligned(4))) void gm_f_sys_fs_u0grkf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sys_fs_u0grkf6 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    int64_t gm_l5; /* h */
    gm_b16_8 gm_l6; /* path */
    uint32_t gm_l7; /* flags */
    gm_t4q2b5ibsrdik gm_l8; /* opts */
    gm_toe5fmr3vvf4d gm_l9; /* err */
    gm_tm98ap4112hee gm_l10; /* f */
    int64_t gm_l11; /* handle */
    gm_tm98ap4112hee gm_l12; /* f */
    gm_b16_8 gm_l13; /* buf */
    gm_tm98ap4112hee gm_l14; /* f */
    gm_b16_8 gm_l15; /* data */
    gm_tm98ap4112hee gm_l16; /* f */
    int64_t gm_l17; /* off */
    int64_t gm_l18; /* whence */
    gm_tm98ap4112hee gm_l19; /* f */
    gm_t9qkh0rsooc8d gm_l20; /* from */
    gm_toe5fmr3vvf4d gm_l21; /* err */
    uint64_t gm_l22; /* pos */
    gm_b16_8 gm_l23; /* path */
    gm_toe5fmr3vvf4d gm_l24; /* err */
    gm_t5j14m8ru132p gm_l25; /* info */
    gm_t2lqu8jsj511d gm_l26; /* sp */
    uint64_t gm_l27; /* count */
    gm_b16_8 gm_l29; /* view */
    gm_t767tucha9lsh gm_l30; /* fields */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* i */
    gm_b16_8 gm_l34; /* from */
    gm_b16_8 gm_l35; /* to */
    gm_b16_8 gm_l36; /* path */
    gm_toe5fmr3vvf4d gm_l37; /* err */
    gm_tm98ap4112hee gm_l38; /* d */
    int64_t gm_l39; /* handle */
    gm_tm98ap4112hee gm_l40; /* d */
    gm_b16_8 gm_l41; /* buf */
    gm_toe5fmr3vvf4d gm_l42; /* err */
    gm_t11t9d8s5diq9 gm_l43; /* next */
    gm_t617vr72bd7b3 gm_l44; /* entry */
    gm_tm98ap4112hee gm_l45; /* d */
    uint64_t gm_l46; /* size */
    gm_tm98ap4112hee gm_l47; /* f */
    gm_tm98ap4112hee gm_l48; /* f */
    gm_b16_8 gm_l49; /* buf */
    gm_b16_8 gm_l50; /* path */
    gm_b16_8 gm_l51; /* buf */
    gm_b16_8 gm_l52; /* path */
    gm_b16_8 gm_l53; /* buf */
    gm_b16_8 gm_l54; /* target */
    gm_b16_8 gm_l55; /* path */
    gm_b16_8 gm_l56; /* path */
    gm_b16_8 gm_l57; /* path */
    GM_STACK_CHECK(&F, 11896u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_l5 = ((int64_t)-1LL);
    }
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t4 = (t3 > ((uint64_t)0ULL));
        if (!t4) goto ie2;
        gm_t9qkh0rsooc8d t5 = gm_l2;
        gm_t9qkh0rsooc8d t6;
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)5ULL));
        gm_t9qkh0rsooc8d t7;
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)10ULL));
        gm_t9qkh0rsooc8d t8;
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)18ULL));
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)11ULL));
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)12ULL));
        gm_t9qkh0rsooc8d t11;
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)13ULL));
        gm_t9qkh0rsooc8d t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)14ULL));
        gm_t9qkh0rsooc8d t13;
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)15ULL));
        gm_t9qkh0rsooc8d t14;
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)21ULL));
        gm_t9qkh0rsooc8d t15;
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)22ULL));
        gm_t9qkh0rsooc8d t16;
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)23ULL));
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)24ULL));
        gm_t9qkh0rsooc8d t18;
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)25ULL));
        gm_t9qkh0rsooc8d t19;
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)26ULL));
        gm_t4qfn59m757do t20;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 0), t6);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 1), t7);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 2), t8);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 3), t9);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 4), t10);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 5), t11);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 6), t12);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 7), t13);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 8), t14);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 9), t15);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 10), t16);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 11), t17);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 12), t18);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 13), t19);
        gm_t4qfn59m757do t21 = t20;
        uint8_t t22 = 0;
        gm_t9qkh0rsooc8d t23 = t5;
        if (gm_eqtje7cegoml(((uint8_t *)&t21) + 0, ((uint8_t *)&t23))) t22 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t21) + 1, ((uint8_t *)&t23))) t22 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t21) + 2, ((uint8_t *)&t23))) t22 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t21) + 3, ((uint8_t *)&t23))) t22 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t21) + 4, ((uint8_t *)&t23))) t22 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t21) + 5, ((uint8_t *)&t23))) t22 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t21) + 6, ((uint8_t *)&t23))) t22 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t21) + 7, ((uint8_t *)&t23))) t22 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t21) + 8, ((uint8_t *)&t23))) t22 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t21) + 9, ((uint8_t *)&t23))) t22 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t21) + 10, ((uint8_t *)&t23))) t22 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t21) + 11, ((uint8_t *)&t23))) t22 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t21) + 12, ((uint8_t *)&t23))) t22 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t21) + 13, ((uint8_t *)&t23))) t22 = 1;
        uint8_t t24 = (uint8_t)(!t22);
        if (!t24) goto ie2;
        {
            void * t26 = gm_l0;
            uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srcn3jjc71a2bv3, 215, 24);
            void * t29 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t27 * 8));
            gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t29) + 96));
            uint64_t t31 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_srcn3jjc71a2bv3, 215, 36);
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t31 * 16));
            gm_b16_8 t34 = t33;
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
            int64_t t36 = gm_f_sys_int_32e79ed(t26, GM_CONV(gm_csguankko4lmhd, t30), t35);
            gm_l5 = t36;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t61) { case ((uint8_t)0ULL): goto ma59; case ((uint8_t)1ULL): goto ma59; case ((uint8_t)2ULL): goto ma59; case ((uint8_t)3ULL): goto ma59; case ((uint8_t)4ULL): goto ma59; case ((uint8_t)5ULL): goto ma38; case ((uint8_t)6ULL): goto ma39; case ((uint8_t)7ULL): goto ma40; case ((uint8_t)8ULL): goto ma41; case ((uint8_t)9ULL): goto ma42; case ((uint8_t)10ULL): goto ma43; case ((uint8_t)11ULL): goto ma44; case ((uint8_t)12ULL): goto ma45; case ((uint8_t)13ULL): goto ma46; case ((uint8_t)14ULL): goto ma47; case ((uint8_t)15ULL): goto ma48; case ((uint8_t)16ULL): goto ma49; case ((uint8_t)17ULL): goto ma50; case ((uint8_t)18ULL): goto ma43; case ((uint8_t)19ULL): goto ma51; case ((uint8_t)20ULL): goto ma52; case ((uint8_t)21ULL): goto ma53; case ((uint8_t)22ULL): goto ma54; case ((uint8_t)23ULL): goto ma55; case ((uint8_t)24ULL): goto ma56; case ((uint8_t)25ULL): goto ma57; case ((uint8_t)26ULL): goto ma58; case ((uint8_t)27ULL): goto ma59; case ((uint8_t)28ULL): goto ma59; case ((uint8_t)29ULL): goto ma59; case ((uint8_t)30ULL): goto ma59; case ((uint8_t)31ULL): goto ma59; case ((uint8_t)32ULL): goto ma59; case ((uint8_t)33ULL): goto ma59; case ((uint8_t)34ULL): goto ma59; case ((uint8_t)35ULL): goto ma59; case ((uint8_t)36ULL): goto ma59; case ((uint8_t)37ULL): goto ma59; case ((uint8_t)38ULL): goto ma59; case ((uint8_t)39ULL): goto ma59; case ((uint8_t)40ULL): goto ma59; case ((uint8_t)41ULL): goto ma59; case ((uint8_t)42ULL): goto ma59; case ((uint8_t)43ULL): goto ma59; case ((uint8_t)44ULL): goto ma59; case ((uint8_t)45ULL): goto ma59; case ((uint8_t)46ULL): goto ma59; case ((uint8_t)47ULL): goto ma59; case ((uint8_t)48ULL): goto ma59; case ((uint8_t)49ULL): goto ma59; case ((uint8_t)50ULL): goto ma59; case ((uint8_t)51ULL): goto ma59; case ((uint8_t)52ULL): goto ma59; case ((uint8_t)53ULL): goto ma59; case ((uint8_t)54ULL): goto ma59; default: goto ma59; }
ma38:;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t63 != ((uint8_t)5ULL)) goto mn62;
        {
            void * t65 = gm_l0;
            gm_tl874t2atnlh3 t66 = gm_l1;
            uint64_t t67 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_srcn3jjc71a2bv3, 219, 41);
            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t67 * 16));
            gm_b16_8 t70 = t69;
            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
            gm_td0ept12e7q8b t72;
            gm_f_sys_src_5svsr6i((void *)&t72, (void *)NULL, cx, NULL, t65, t66, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t73 = t72;
            if (((((uint8_t *)&t73))[0] != 0)) {
                gm_t76123gfet73g t74 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t73) + 8));
                gm_tarq9lp8tdk6f t75;
                GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                ((uint8_t *)&t75)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t75 + 8), t74);
                F.ret = t75;
                goto gm_fret;
            }
            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
            gm_l6 = t76;
        }
        {
            void * t77 = gm_l0;
            uint64_t t78 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_srcn3jjc71a2bv3, 220, 33);
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t78 * 16));
            gm_b16_8 t81 = t80;
            gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
            unsigned __int128 t83 = gm_f_get_uint_bq8c4b7(t77, t82);
            uint32_t t84 = (uint32_t)t83;
            gm_l7 = t84;
        }
        {
            uint32_t t85 = gm_l7;
            uint8_t t86 = gm_f_sys_has_0sego5l(t85, ((uint32_t)1ULL));
            uint32_t t87 = gm_l7;
            uint8_t t88 = gm_f_sys_has_0sego5l(t87, ((uint32_t)2ULL));
            uint32_t t89 = gm_l7;
            uint8_t t90 = gm_f_sys_has_0sego5l(t89, ((uint32_t)4ULL));
            uint32_t t91 = gm_l7;
            uint8_t t92 = gm_f_sys_has_0sego5l(t91, ((uint32_t)8ULL));
            uint32_t t93 = gm_l7;
            uint8_t t94 = gm_f_sys_has_0sego5l(t93, ((uint32_t)16ULL));
            uint32_t t95 = gm_l7;
            uint8_t t96 = gm_f_sys_has_0sego5l(t95, ((uint32_t)32ULL));
            gm_t4q2b5ibsrdik t97;
            GM_ST(uint8_t, ((uint8_t *)&t97 + 0), t86);
            GM_ST(uint8_t, ((uint8_t *)&t97 + 1), t88);
            GM_ST(uint8_t, ((uint8_t *)&t97 + 2), t90);
            GM_ST(uint8_t, ((uint8_t *)&t97 + 3), t92);
            GM_ST(uint8_t, ((uint8_t *)&t97 + 4), t94);
            GM_ST(uint8_t, ((uint8_t *)&t97 + 5), t96);
            gm_l8 = t97;
        }
        {
            gm_b16_8 t98 = gm_l6;
            gm_t4q2b5ibsrdik t99 = gm_l8;
            gm_node *pn100 = gm_find(cx, 2126215853);
            if (!pn100) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 229, 18);
            gm_t62ebbi2i5rkd t101;
            t101 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn100->clauses[0])(pn100->env, pn100->hctx, pn100, t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t62ebbi2i5rkd t102 = t101;
            gm_tm98ap4112hee t103;
            if (!((((uint8_t *)&t102))[0] != 0)) {
                gm_tm98ap4112hee t104 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t102) + 8));
                t103 = t104;
            } else {
                gm_toe5fmr3vvf4d t105 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t102) + 8));
                gm_l9 = t105;
                gm_toe5fmr3vvf4d t106 = gm_l9;
                int64_t t107 = gm_f_sys_code_cjtlfo1(t106);
                gm_tarq9lp8tdk6f t108;
                GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t108 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t108 + 24, 0);
                ((uint8_t *)&t108)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t108 + 8), t107);
                gm_tarq9lp8tdk6f t109 = t108;
                uint8_t t110 = ((((uint8_t *)&t109))[0] != 0);
                F.ret = t108;
                goto gm_fret;
            }
            gm_l10 = t103;
        }
        {
            void * t111 = gm_l0;
            void * t112 = (void *)(((uint8_t *)t111) + 1352);
            int64_t t113 = gm_f_sys_next_handle_r35ek81(t112);
            gm_l11 = t113;
        }
        {
            void * t114 = gm_l0;
            void * t115 = (void *)((((uint8_t *)t114) + 1352) + 0);
            int64_t t116 = gm_l11;
            gm_tm98ap4112hee t117 = gm_l10;
            gm_tftb1odc739t5 t118 = gm_f_put_endcgjj(t115, t116, GM_CONV(gm_csfq64m1m1rt6m, t117));
            gm_tftb1odc739t5 t119 = t118;
            if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 1, gm_srcn3jjc71a2bv3, 231, 13);
            gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
        }
        {
            int64_t t121 = gm_l11;
            gm_tarq9lp8tdk6f t122;
            GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t122 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t122 + 24, 0);
            ((uint8_t *)&t122)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t122 + 8), t121);
            gm_tarq9lp8tdk6f t123 = t122;
            uint8_t t124 = ((((uint8_t *)&t123))[0] != 0);
            F.ret = t122;
            goto gm_fret;
        }
        goto me37;
mn62:;
ma39:;
        uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t126 != ((uint8_t)6ULL)) goto mn125;
        {
            void * t128 = gm_l0;
            void * t129 = (void *)((((uint8_t *)t128) + 1352) + 0);
            int64_t t130 = gm_l5;
            gm_tppo2hmut88dq t131 = gm_f_remove_82qm8hc(t129, t130);
            gm_tppo2hmut88dq t132 = t131;
            gm_tm98ap4112hee t133;
            if (((((uint8_t *)&t132))[8] != 0)) {
                gm_tm98ap4112hee t134 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t132) + 0));
                t133 = t134;
            } else {
                int64_t t135 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t136;
                GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t136 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t136 + 24, 0);
                ((uint8_t *)&t136)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t136 + 8), t135);
                gm_tarq9lp8tdk6f t137 = t136;
                uint8_t t138 = ((((uint8_t *)&t137))[0] != 0);
                F.ret = t136;
                goto gm_fret;
            }
            gm_l12 = t133;
        }
        {
            gm_tm98ap4112hee t139 = gm_l12;
            gm_node *pn140 = gm_find(cx, 2126215853);
            if (!pn140) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 236, 13);
            gm_b0_1 t141;
            t141 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn140->clauses[1])(pn140->env, pn140->hctx, pn140, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tarq9lp8tdk6f t142;
            GM_ST(uint8_t, (uint8_t *)&t142 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t142 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t142 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t142 + 24, 0);
            ((uint8_t *)&t142)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t142 + 8), ((int64_t)0LL));
            gm_tarq9lp8tdk6f t143 = t142;
            uint8_t t144 = ((((uint8_t *)&t143))[0] != 0);
            F.ret = t142;
            goto gm_fret;
        }
        goto me37;
mn125:;
ma40:;
        uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t146 != ((uint8_t)7ULL)) goto mn145;
        {
            void * t148 = gm_l0;
            gm_tl874t2atnlh3 t149 = gm_l1;
            uint64_t t150 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t151 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t150 >= (uintptr_t)(t151)) gm_trap_index(t150, t151, gm_srcn3jjc71a2bv3, 240, 41);
            gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t150 * 16));
            gm_b16_8 t153 = t152;
            gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&t153));
            gm_td0ept12e7q8b t155;
            gm_f_sys_dest_rtrb14h((void *)&t155, (void *)NULL, cx, NULL, t148, t149, t154);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t156 = t155;
            if (((((uint8_t *)&t156))[0] != 0)) {
                gm_t76123gfet73g t157 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t156) + 8));
                gm_tarq9lp8tdk6f t158;
                GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
                ((uint8_t *)&t158)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t158 + 8), t157);
                F.ret = t158;
                goto gm_fret;
            }
            gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&t156) + 8));
            gm_l13 = t159;
        }
        {
            void * t160 = gm_l0;
            void * t161 = (void *)((((uint8_t *)t160) + 1352) + 0);
            int64_t t162 = gm_l5;
            gm_tppo2hmut88dq t163 = gm_f_get_f2mbmm8(t161, t162);
            gm_tppo2hmut88dq t164 = t163;
            gm_tm98ap4112hee t165;
            if (((((uint8_t *)&t164))[8] != 0)) {
                gm_tm98ap4112hee t166 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t164) + 0));
                t165 = t166;
            } else {
                int64_t t167 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t168;
                GM_ST(uint8_t, (uint8_t *)&t168 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t168 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t168 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t168 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t168 + 24, 0);
                ((uint8_t *)&t168)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t168 + 8), t167);
                gm_tarq9lp8tdk6f t169 = t168;
                uint8_t t170 = ((((uint8_t *)&t169))[0] != 0);
                F.ret = t168;
                goto gm_fret;
            }
            gm_l14 = t165;
        }
        {
            void * t172 = gm_l0;
            uint64_t t173 = GM_LD(uintptr_t, (((((uint8_t *)t172) + 1656) + 0) + 0) + 8);
            uint8_t t174 = (t173 > ((uint64_t)1ULL));
            if (!t174) goto ie171;
            {
                void * t176 = gm_l0;
                gm_tl874t2atnlh3 t177 = gm_l1;
                gm_t3olsle24rveu t178;
                gm_tm98ap4112hee t179 = gm_l14;
                GM_ST(gm_tm98ap4112hee, (uint8_t *)&t178 + 0, t179);
                gm_b16_8 t180 = gm_l13;
                GM_ST(gm_b16_8, (uint8_t *)&t178 + 8, t180);
                gm_tmvdbo28o1i6h t181;
                GM_ST(uint8_t, (uint8_t *)&t181 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t181 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t181 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t181 + 0), ((uint8_t)1ULL));
                GM_ST(gm_t3olsle24rveu, ((uint8_t *)&t181 + 8), t178);
                gm_t6j8vdmi81h7c t182;
                gm_f_on_host_thread_kmegc07((void *)&t182, (void *)NULL, cx, NULL, t176, t177, &t181);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6j8vdmi81h7c t183 = t182;
                if (((((uint8_t *)&t183))[0] != 0)) {
                    gm_t76123gfet73g t184 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t183) + 8));
                    gm_tarq9lp8tdk6f t185;
                    GM_ST(uint8_t, (uint8_t *)&t185 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t185 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t185 + 4, 0);
                    ((uint8_t *)&t185)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t185 + 8), t184);
                    F.ret = t185;
                    goto gm_fret;
                }
                gm_tfvg1o4f8mmhi t186 = GM_LD(gm_tfvg1o4f8mmhi, (((uint8_t *)&t183) + 8));
                gm_tfvg1o4f8mmhi t187 = t186;
                gm_thf5kr0is9u9i t188 = GM_LD(gm_thf5kr0is9u9i, (((uint8_t *)&t187) + 0));
                int64_t t189 = gm_f_sys_count_50c5lcp(t188);
                gm_tarq9lp8tdk6f t190;
                GM_ST(uint8_t, (uint8_t *)&t190 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t190 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t190 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t190 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t190 + 24, 0);
                ((uint8_t *)&t190)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t190 + 8), t189);
                gm_tarq9lp8tdk6f t191 = t190;
                uint8_t t192 = ((((uint8_t *)&t191))[0] != 0);
                F.ret = t190;
                goto gm_fret;
            }
            goto ix171;
ie171:;
ix171:;
        }
        {
            gm_tm98ap4112hee t193 = gm_l14;
            gm_b16_8 t194 = gm_l13;
            gm_node *pn195 = gm_find(cx, 2126215853);
            if (!pn195) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 243, 30);
            gm_thf5kr0is9u9i t196;
            t196 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn195->clauses[2])(pn195->env, pn195->hctx, pn195, t193, t194);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t197 = gm_f_sys_count_50c5lcp(t196);
            gm_tarq9lp8tdk6f t198;
            GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t198 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t198 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t198 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t198 + 24, 0);
            ((uint8_t *)&t198)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t198 + 8), t197);
            gm_tarq9lp8tdk6f t199 = t198;
            uint8_t t200 = ((((uint8_t *)&t199))[0] != 0);
            F.ret = t198;
            goto gm_fret;
        }
        goto me37;
mn145:;
ma41:;
        uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t202 != ((uint8_t)8ULL)) goto mn201;
        {
            void * t204 = gm_l0;
            gm_tl874t2atnlh3 t205 = gm_l1;
            uint64_t t206 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t207 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t206 >= (uintptr_t)(t207)) gm_trap_index(t206, t207, gm_srcn3jjc71a2bv3, 246, 41);
            gm_b16_8 t208 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t206 * 16));
            gm_b16_8 t209 = t208;
            gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&t209));
            gm_td0ept12e7q8b t211;
            gm_f_sys_src_5svsr6i((void *)&t211, (void *)NULL, cx, NULL, t204, t205, t210);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t212 = t211;
            if (((((uint8_t *)&t212))[0] != 0)) {
                gm_t76123gfet73g t213 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t212) + 8));
                gm_tarq9lp8tdk6f t214;
                GM_ST(uint8_t, (uint8_t *)&t214 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t214 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t214 + 4, 0);
                ((uint8_t *)&t214)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t214 + 8), t213);
                F.ret = t214;
                goto gm_fret;
            }
            gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)&t212) + 8));
            gm_l15 = t215;
        }
        {
            void * t216 = gm_l0;
            void * t217 = (void *)((((uint8_t *)t216) + 1352) + 0);
            int64_t t218 = gm_l5;
            gm_tppo2hmut88dq t219 = gm_f_get_f2mbmm8(t217, t218);
            gm_tppo2hmut88dq t220 = t219;
            gm_tm98ap4112hee t221;
            if (((((uint8_t *)&t220))[8] != 0)) {
                gm_tm98ap4112hee t222 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t220) + 0));
                t221 = t222;
            } else {
                int64_t t223 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t224;
                GM_ST(uint8_t, (uint8_t *)&t224 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t224 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t224 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t224 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t224 + 24, 0);
                ((uint8_t *)&t224)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t224 + 8), t223);
                gm_tarq9lp8tdk6f t225 = t224;
                uint8_t t226 = ((((uint8_t *)&t225))[0] != 0);
                F.ret = t224;
                goto gm_fret;
            }
            gm_l16 = t221;
        }
        {
            gm_tm98ap4112hee t227 = gm_l16;
            gm_b16_8 t228 = gm_l15;
            gm_node *pn229 = gm_find(cx, 2126215853);
            if (!pn229) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 248, 30);
            gm_thf5kr0is9u9i t230;
            t230 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn229->clauses[3])(pn229->env, pn229->hctx, pn229, t227, t228);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t231 = gm_f_sys_count_50c5lcp(t230);
            gm_tarq9lp8tdk6f t232;
            GM_ST(uint8_t, (uint8_t *)&t232 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t232 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t232 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t232 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t232 + 24, 0);
            ((uint8_t *)&t232)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t232 + 8), t231);
            gm_tarq9lp8tdk6f t233 = t232;
            uint8_t t234 = ((((uint8_t *)&t233))[0] != 0);
            F.ret = t232;
            goto gm_fret;
        }
        goto me37;
mn201:;
ma42:;
        uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t236 != ((uint8_t)9ULL)) goto mn235;
        {
            void * t238 = gm_l0;
            uint64_t t239 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t240 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t239 >= (uintptr_t)(t240)) gm_trap_index(t239, t240, gm_srcn3jjc71a2bv3, 251, 31);
            void * t241 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t239 * 8));
            gm_t88rcucg1b7uk t242 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t241) + 96));
            uint64_t t243 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t244 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t243 >= (uintptr_t)(t244)) gm_trap_index(t243, t244, gm_srcn3jjc71a2bv3, 251, 43);
            gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t243 * 16));
            gm_b16_8 t246 = t245;
            gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)&t246));
            int64_t t248 = gm_f_sys_int_32e79ed(t238, GM_CONV(gm_csguankko4lmhd, t242), t247);
            gm_l17 = t248;
        }
        {
            void * t249 = gm_l0;
            uint64_t t250 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t251 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t250 >= (uintptr_t)(t251)) gm_trap_index(t250, t251, gm_srcn3jjc71a2bv3, 252, 34);
            void * t252 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t250 * 8));
            gm_t88rcucg1b7uk t253 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t252) + 96));
            uint64_t t254 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t255 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t254 >= (uintptr_t)(t255)) gm_trap_index(t254, t255, gm_srcn3jjc71a2bv3, 252, 46);
            gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t254 * 16));
            gm_b16_8 t257 = t256;
            gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)&t257));
            int64_t t259 = gm_f_sys_int_32e79ed(t249, GM_CONV(gm_csguankko4lmhd, t253), t258);
            gm_l18 = t259;
        }
        {
            void * t260 = gm_l0;
            void * t261 = (void *)((((uint8_t *)t260) + 1352) + 0);
            int64_t t262 = gm_l5;
            gm_tppo2hmut88dq t263 = gm_f_get_f2mbmm8(t261, t262);
            gm_tppo2hmut88dq t264 = t263;
            gm_tm98ap4112hee t265;
            if (((((uint8_t *)&t264))[8] != 0)) {
                gm_tm98ap4112hee t266 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t264) + 0));
                t265 = t266;
            } else {
                int64_t t267 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t268;
                GM_ST(uint8_t, (uint8_t *)&t268 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t268 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t268 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t268 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t268 + 24, 0);
                ((uint8_t *)&t268)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t268 + 8), t267);
                gm_tarq9lp8tdk6f t269 = t268;
                uint8_t t270 = ((((uint8_t *)&t269))[0] != 0);
                F.ret = t268;
                goto gm_fret;
            }
            gm_l19 = t265;
        }
        {
            int64_t t272 = gm_l18;
            uint8_t t273 = ((t272 == ((int64_t)0LL)));
            if (!t273) goto ie271;
            int64_t t274 = gm_l17;
            uint8_t t275 = (t274 < ((int64_t)0LL));
            if (!t275) goto ie271;
            {
                int64_t t277 = GM_LD(int64_t, gm_cndfnv7mbeh);
                gm_tarq9lp8tdk6f t278;
                GM_ST(uint8_t, (uint8_t *)&t278 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t278 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t278 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t278 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t278 + 24, 0);
                ((uint8_t *)&t278)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t278 + 8), t277);
                gm_tarq9lp8tdk6f t279 = t278;
                uint8_t t280 = ((((uint8_t *)&t279))[0] != 0);
                F.ret = t278;
                goto gm_fret;
            }
            goto ix271;
ie271:;
ix271:;
        }
        {
            int64_t t281 = gm_l18;
            int64_t t282 = t281;
            gm_t9qkh0rsooc8d t283;
            int64_t t286 = GM_LD(int64_t, ((uint8_t *)&t282));
            if (!(t286 == ((int64_t)0LL))) goto mn285;
            gm_t9qkh0rsooc8d t287;
            GM_ST(uint8_t, ((uint8_t *)&t287 + 0), ((uint8_t)0ULL));
            t283 = t287;
            goto me284;
mn285:;
            int64_t t289 = GM_LD(int64_t, ((uint8_t *)&t282));
            if (!(t289 == ((int64_t)1LL))) goto mn288;
            gm_t9qkh0rsooc8d t290;
            GM_ST(uint8_t, ((uint8_t *)&t290 + 0), ((uint8_t)1ULL));
            t283 = t290;
            goto me284;
mn288:;
            int64_t t292 = GM_LD(int64_t, ((uint8_t *)&t282));
            if (!(t292 == ((int64_t)2LL))) goto mn291;
            gm_t9qkh0rsooc8d t293;
            GM_ST(uint8_t, ((uint8_t *)&t293 + 0), ((uint8_t)2ULL));
            t283 = t293;
            goto me284;
mn291:;
            int64_t t295 = GM_LD(int64_t, gm_cndfnv7mbeh);
            gm_tarq9lp8tdk6f t296;
            GM_ST(uint8_t, (uint8_t *)&t296 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t296 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t296 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t296 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t296 + 24, 0);
            ((uint8_t *)&t296)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t296 + 8), t295);
            gm_tarq9lp8tdk6f t297 = t296;
            uint8_t t298 = ((((uint8_t *)&t297))[0] != 0);
            F.ret = t296;
            goto gm_fret;
            goto me284;
mn294:;
            gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 255, 33);
me284:;
            gm_l20 = t283;
        }
        {
            gm_tm98ap4112hee t299 = gm_l19;
            int64_t t300 = gm_l17;
            gm_t9qkh0rsooc8d t301 = gm_l20;
            gm_node *pn302 = gm_find(cx, 2126215853);
            if (!pn302) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 261, 20);
            gm_thf5kr0is9u9i t303;
            t303 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, int64_t, gm_t9qkh0rsooc8d))pn302->clauses[4])(pn302->env, pn302->hctx, pn302, t299, t300, t301);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thf5kr0is9u9i t304 = t303;
            uint64_t t305;
            if (!((((uint8_t *)&t304))[0] != 0)) {
                uint64_t t306 = GM_LD(uint64_t, (((uint8_t *)&t304) + 8));
                t305 = t306;
            } else {
                gm_toe5fmr3vvf4d t307 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t304) + 8));
                gm_l21 = t307;
                gm_toe5fmr3vvf4d t308 = gm_l21;
                int64_t t309 = gm_f_sys_code_cjtlfo1(t308);
                gm_tarq9lp8tdk6f t310;
                GM_ST(uint8_t, (uint8_t *)&t310 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t310 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t310 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t310 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t310 + 24, 0);
                ((uint8_t *)&t310)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t310 + 8), t309);
                gm_tarq9lp8tdk6f t311 = t310;
                uint8_t t312 = ((((uint8_t *)&t311))[0] != 0);
                F.ret = t310;
                goto gm_fret;
            }
            gm_l22 = t305;
        }
        {
            uint64_t t313 = gm_l22;
            uint64_t t314 = t313;
            int64_t t315;
            memset(&t315, 0, sizeof t315); memcpy(&t315, ((uint8_t *)&t314), sizeof t315 < 8 ? sizeof t315 : 8);
            gm_tarq9lp8tdk6f t316;
            GM_ST(uint8_t, (uint8_t *)&t316 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t316 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t316 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t316 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t316 + 24, 0);
            ((uint8_t *)&t316)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t316 + 8), t315);
            gm_tarq9lp8tdk6f t317 = t316;
            uint8_t t318 = ((((uint8_t *)&t317))[0] != 0);
            F.ret = t316;
            goto gm_fret;
        }
        goto me37;
mn235:;
ma43:;
        uint8_t t322 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t322 != ((uint8_t)10ULL)) goto pa321;
        goto po320;
pa321:;
        uint8_t t324 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t324 != ((uint8_t)18ULL)) goto pa323;
        goto po320;
pa323:;
        goto mn319;
po320:;
        {
            void * t326 = gm_l0;
            gm_tl874t2atnlh3 t327 = gm_l1;
            uint64_t t328 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t329 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t328 >= (uintptr_t)(t329)) gm_trap_index(t328, t329, gm_srcn3jjc71a2bv3, 265, 41);
            gm_b16_8 t330 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t328 * 16));
            gm_b16_8 t331 = t330;
            gm_b16_8 t332 = GM_LD(gm_b16_8, ((uint8_t *)&t331));
            gm_td0ept12e7q8b t333;
            gm_f_sys_src_5svsr6i((void *)&t333, (void *)NULL, cx, NULL, t326, t327, t332);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t334 = t333;
            if (((((uint8_t *)&t334))[0] != 0)) {
                gm_t76123gfet73g t335 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t334) + 8));
                gm_tarq9lp8tdk6f t336;
                GM_ST(uint8_t, (uint8_t *)&t336 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t336 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t336 + 4, 0);
                ((uint8_t *)&t336)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t336 + 8), t335);
                F.ret = t336;
                goto gm_fret;
            }
            gm_b16_8 t337 = GM_LD(gm_b16_8, (((uint8_t *)&t334) + 8));
            gm_l23 = t337;
        }
        {
            gm_tih6h4avv5nrb t338;
            uint8_t t340 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)10ULL)));
            if (!t340) goto ie339;
            gm_b16_8 t342 = gm_l23;
            gm_node *pn343 = gm_find(cx, 2126215853);
            if (!pn343) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 266, 41);
            gm_tih6h4avv5nrb t344;
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn343->clauses[5])((void *)&t344, pn343->env, pn343->hctx, pn343, t342);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tih6h4avv5nrb t345 = t344;
            t338 = t345;
            goto ix339;
ie339:;
            gm_b16_8 t347 = gm_l23;
            gm_node *pn348 = gm_find(cx, 2126215853);
            if (!pn348) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 266, 64);
            gm_tih6h4avv5nrb t349;
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn348->clauses[13])((void *)&t349, pn348->env, pn348->hctx, pn348, t347);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tih6h4avv5nrb t350 = t349;
            t338 = t350;
ix339:;
            gm_tih6h4avv5nrb t351 = t338;
            gm_t5j14m8ru132p t352;
            if (!((((uint8_t *)&t351))[0] != 0)) {
                gm_t5j14m8ru132p t353 = GM_LD(gm_t5j14m8ru132p, (((uint8_t *)&t351) + 8));
                t352 = t353;
            } else {
                gm_toe5fmr3vvf4d t354 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t351) + 8));
                gm_l24 = t354;
                gm_toe5fmr3vvf4d t355 = gm_l24;
                int64_t t356 = gm_f_sys_code_cjtlfo1(t355);
                gm_tarq9lp8tdk6f t357;
                GM_ST(uint8_t, (uint8_t *)&t357 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t357 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t357 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t357 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t357 + 24, 0);
                ((uint8_t *)&t357)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t357 + 8), t356);
                gm_tarq9lp8tdk6f t358 = t357;
                uint8_t t359 = ((((uint8_t *)&t358))[0] != 0);
                F.ret = t357;
                goto gm_fret;
            }
            gm_l25 = t352;
        }
        {
            void * t360 = gm_l0;
            uint64_t t361 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t362 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t361 >= (uintptr_t)(t362)) gm_trap_index(t361, t362, gm_srcn3jjc71a2bv3, 267, 33);
            gm_b16_8 t363 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t361 * 16));
            gm_b16_8 t364 = t363;
            gm_b16_8 t365 = GM_LD(gm_b16_8, ((uint8_t *)&t364));
            gm_t2lqu8jsj511d t366 = gm_f_slice_parts_1se5hu4(t360, t365);
            gm_l26 = t366;
        }
        {
            uint64_t t367 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
            uint64_t t368 = (t367 < ((uint64_t)4ULL) ? t367 : ((uint64_t)4ULL));
            gm_l27 = t368;
        }
        {
            void * t369 = gm_l0;
            void * t370 = (void *)(((uint8_t *)t369) + 184);
            uint64_t t371 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 0));
            uint64_t t372 = gm_l27;
            uint64_t r373; bool o374 = __builtin_mul_overflow(t372, ((uint64_t)8ULL), &r373);
            if (o374) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 269, 46);
            uint64_t t375 = r373;
            gm_t934k1j7u70ct t376;
            gm_f_write_view_pebit6h((void *)&t376, t370, t371, t375, ((uint64_t)8ULL));
            gm_t934k1j7u70ct t377 = t376;
            gm_b16_8 t378;
            if (!((((uint8_t *)&t377))[0] != 0)) {
                gm_b16_8 t379 = GM_LD(gm_b16_8, (((uint8_t *)&t377) + 8));
                t378 = t379;
            } else {
                gm_tpekhfs32op0f t380 = GM_LD(gm_tpekhfs32op0f, (((uint8_t *)&t377) + 8));
                F.l28 = t380;
                void * t381 = gm_l0;
                gm_tl874t2atnlh3 t382 = gm_l1;
                gm_t76123gfet73g t383;
                gm_f_mem_trap_ecogko0((void *)&t383, (void *)NULL, cx, NULL, t381, t382, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l28)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tarq9lp8tdk6f t384;
                GM_ST(uint8_t, (uint8_t *)&t384 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t384 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t384 + 4, 0);
                ((uint8_t *)&t384)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t384 + 8), t383);
                gm_tarq9lp8tdk6f t385 = t384;
                uint8_t t386 = ((((uint8_t *)&t385))[0] != 0);
                F.ret = t384;
                goto gm_fret;
            }
            gm_l29 = t378;
        }
        {
            uint64_t t387 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 0));
            uint64_t t388 = t387;
            int64_t t389;
            memset(&t389, 0, sizeof t389); memcpy(&t389, ((uint8_t *)&t388), sizeof t389 < 8 ? sizeof t389 : 8);
            GM_ST(int64_t, (uint8_t *)&gm_l30 + 0, t389);
            gm_t9qkh0rsooc8d t390 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l25) + 8));
            int64_t t391 = gm_f_sys_kind_sjfdqbt(t390);
            GM_ST(int64_t, (uint8_t *)&gm_l30 + 8, t391);
            int64_t t392 = GM_LD(int64_t, (((uint8_t *)&gm_l25) + 16));
            GM_ST(int64_t, (uint8_t *)&gm_l30 + 16, t392);
            uint32_t t393 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 24));
            int64_t t394 = (int64_t)t393;
            GM_ST(int64_t, (uint8_t *)&gm_l30 + 24, t394);
        }
        {
            {
                gm_l32 = ((uint64_t)0ULL);
            }
            {
                uint64_t t396 = gm_l27;
                uint64_t t397 = gm_f_us_cuv01fp(t396);
                gm_l31 = t397;
            }
            {
                for (;;) {
                    uint64_t t399 = gm_l32;
                    uint64_t t400 = gm_l31;
                    uint8_t t401 = (t399 < t400);
                    if (!t401) goto lb398;
                    {
                        uint64_t t404 = gm_l32;
                        uint64_t t405 = t404;
                        uint64_t t408 = GM_LD(uint64_t, ((uint8_t *)&t405));
                        gm_l33 = t408;
                        {
                            void * t410 = gm_l0;
                            uint64_t t411 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                            void * t412 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)));
                            uint64_t t413 = gm_l33;
                            uint64_t r414; bool o415 = __builtin_mul_overflow(t413, ((uint64_t)8ULL), &r414);
                            if (o415) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 271, 53);
                            uint64_t t416 = r414;
                            uint64_t t417 = (uintptr_t)t416;
                            uint64_t t418 = gm_l33;
                            uint64_t r419; bool o420 = __builtin_add_overflow(t418, ((uint64_t)1ULL), &r419);
                            if (o420) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 271, 61);
                            uint64_t t421 = r419;
                            uint64_t r422; bool o423 = __builtin_mul_overflow(t421, ((uint64_t)8ULL), &r422);
                            if (o423) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 271, 60);
                            uint64_t t424 = r422;
                            uint64_t t425 = (uintptr_t)t424;
                            if (t417 > t425 || t425 > (uintptr_t)t411) gm_trap_slice(t417, t425, t411, gm_srcn3jjc71a2bv3, 271, 48);
                            gm_b16_8 t426;
                            GM_ST(void *, (uint8_t *)&t426, (void *)(((uint8_t *)t412 + t417 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t426 + 8, (uintptr_t)(t425 - t417));
                            uint64_t t427 = gm_l33;
                            uint64_t t428 = (uintptr_t)t427;
                            if (t428 >= (uintptr_t)(4)) gm_trap_index(t428, 4, gm_srcn3jjc71a2bv3, 271, 79);
                            int64_t t429 = GM_LD(int64_t, (((uint8_t *)&gm_l30) + t428 * 8));
                            int64_t t430 = t429;
                            uint64_t t431;
                            memset(&t431, 0, sizeof t431); memcpy(&t431, ((uint8_t *)&t430), sizeof t431 < 8 ? sizeof t431 : 8);
                            unsigned __int128 t432 = (unsigned __int128)t431;
                            gm_f_put_uint_cddin99(t410, t426, t432);
                        }
                        goto me406;
mn407:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 271, 13);
me406:;
                    }
lc398:;
                    {
                        uint64_t t434 = gm_l32;
                        uint64_t r435; bool o436 = __builtin_add_overflow(t434, ((uint64_t)1ULL), &r435);
                        uint64_t t437 = r435;
                        gm_l32 = t437;
                    }
                }
lb398:;
            }
        }
        {
            gm_tarq9lp8tdk6f t438;
            GM_ST(uint8_t, (uint8_t *)&t438 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t438 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t438 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t438 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t438 + 24, 0);
            ((uint8_t *)&t438)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t438 + 8), ((int64_t)0LL));
            gm_tarq9lp8tdk6f t439 = t438;
            uint8_t t440 = ((((uint8_t *)&t439))[0] != 0);
            F.ret = t438;
            goto gm_fret;
        }
        goto me37;
mn319:;
ma44:;
        uint8_t t442 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t442 != ((uint8_t)11ULL)) goto mn441;
        void * t443 = gm_l0;
        gm_tl874t2atnlh3 t444 = gm_l1;
        uint64_t t445 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t446 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t445 >= (uintptr_t)(t446)) gm_trap_index(t445, t446, gm_srcn3jjc71a2bv3, 274, 68);
        gm_b16_8 t447 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t445 * 16));
        gm_b16_8 t448 = t447;
        gm_b16_8 t449 = GM_LD(gm_b16_8, ((uint8_t *)&t448));
        gm_td0ept12e7q8b t450;
        gm_f_sys_src_5svsr6i((void *)&t450, (void *)NULL, cx, NULL, t443, t444, t449);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_td0ept12e7q8b t451 = t450;
        if (((((uint8_t *)&t451))[0] != 0)) {
            gm_t76123gfet73g t452 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t451) + 8));
            gm_tarq9lp8tdk6f t453;
            GM_ST(uint8_t, (uint8_t *)&t453 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t453 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t453 + 4, 0);
            ((uint8_t *)&t453)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t453 + 8), t452);
            F.ret = t453;
            goto gm_fret;
        }
        gm_b16_8 t454 = GM_LD(gm_b16_8, (((uint8_t *)&t451) + 8));
        gm_node *pn455 = gm_find(cx, 2126215853);
        if (!pn455) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 274, 38);
        gm_ttln0d0hd70en t456;
        t456 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn455->clauses[6])(pn455->env, pn455->hctx, pn455, t454);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        int64_t t457 = gm_f_sys_unit_nrsqciq(t456);
        gm_tarq9lp8tdk6f t458;
        GM_ST(uint8_t, (uint8_t *)&t458 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t458 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t458 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t458 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t458 + 24, 0);
        ((uint8_t *)&t458)[0] = 0;
        GM_ST(int64_t, ((uint8_t *)&t458 + 8), t457);
        gm_tarq9lp8tdk6f t459 = t458;
        uint8_t t460 = ((((uint8_t *)&t459))[0] != 0);
        F.ret = t458;
        goto gm_fret;
        goto me37;
mn441:;
ma45:;
        uint8_t t462 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t462 != ((uint8_t)12ULL)) goto mn461;
        {
            void * t464 = gm_l0;
            gm_tl874t2atnlh3 t465 = gm_l1;
            uint64_t t466 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t467 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t466 >= (uintptr_t)(t467)) gm_trap_index(t466, t467, gm_srcn3jjc71a2bv3, 276, 41);
            gm_b16_8 t468 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t466 * 16));
            gm_b16_8 t469 = t468;
            gm_b16_8 t470 = GM_LD(gm_b16_8, ((uint8_t *)&t469));
            gm_td0ept12e7q8b t471;
            gm_f_sys_src_5svsr6i((void *)&t471, (void *)NULL, cx, NULL, t464, t465, t470);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t472 = t471;
            if (((((uint8_t *)&t472))[0] != 0)) {
                gm_t76123gfet73g t473 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t472) + 8));
                gm_tarq9lp8tdk6f t474;
                GM_ST(uint8_t, (uint8_t *)&t474 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t474 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t474 + 4, 0);
                ((uint8_t *)&t474)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t474 + 8), t473);
                F.ret = t474;
                goto gm_fret;
            }
            gm_b16_8 t475 = GM_LD(gm_b16_8, (((uint8_t *)&t472) + 8));
            gm_l34 = t475;
        }
        {
            void * t476 = gm_l0;
            gm_tl874t2atnlh3 t477 = gm_l1;
            uint64_t t478 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t479 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t478 >= (uintptr_t)(t479)) gm_trap_index(t478, t479, gm_srcn3jjc71a2bv3, 277, 39);
            gm_b16_8 t480 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t478 * 16));
            gm_b16_8 t481 = t480;
            gm_b16_8 t482 = GM_LD(gm_b16_8, ((uint8_t *)&t481));
            gm_td0ept12e7q8b t483;
            gm_f_sys_src_5svsr6i((void *)&t483, (void *)NULL, cx, NULL, t476, t477, t482);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t484 = t483;
            if (((((uint8_t *)&t484))[0] != 0)) {
                gm_t76123gfet73g t485 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t484) + 8));
                gm_tarq9lp8tdk6f t486;
                GM_ST(uint8_t, (uint8_t *)&t486 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t486 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t486 + 4, 0);
                ((uint8_t *)&t486)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t486 + 8), t485);
                F.ret = t486;
                goto gm_fret;
            }
            gm_b16_8 t487 = GM_LD(gm_b16_8, (((uint8_t *)&t484) + 8));
            gm_l35 = t487;
        }
        {
            gm_b16_8 t488 = gm_l34;
            gm_b16_8 t489 = gm_l35;
            gm_node *pn490 = gm_find(cx, 2126215853);
            if (!pn490) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 278, 29);
            gm_ttln0d0hd70en t491;
            t491 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn490->clauses[7])(pn490->env, pn490->hctx, pn490, t488, t489);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t492 = gm_f_sys_unit_nrsqciq(t491);
            gm_tarq9lp8tdk6f t493;
            GM_ST(uint8_t, (uint8_t *)&t493 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t493 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t493 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t493 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t493 + 24, 0);
            ((uint8_t *)&t493)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t493 + 8), t492);
            gm_tarq9lp8tdk6f t494 = t493;
            uint8_t t495 = ((((uint8_t *)&t494))[0] != 0);
            F.ret = t493;
            goto gm_fret;
        }
        goto me37;
mn461:;
ma46:;
        uint8_t t497 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t497 != ((uint8_t)13ULL)) goto mn496;
        void * t498 = gm_l0;
        gm_tl874t2atnlh3 t499 = gm_l1;
        uint64_t t500 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t501 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t500 >= (uintptr_t)(t501)) gm_trap_index(t500, t501, gm_srcn3jjc71a2bv3, 280, 71);
        gm_b16_8 t502 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t500 * 16));
        gm_b16_8 t503 = t502;
        gm_b16_8 t504 = GM_LD(gm_b16_8, ((uint8_t *)&t503));
        gm_td0ept12e7q8b t505;
        gm_f_sys_src_5svsr6i((void *)&t505, (void *)NULL, cx, NULL, t498, t499, t504);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_td0ept12e7q8b t506 = t505;
        if (((((uint8_t *)&t506))[0] != 0)) {
            gm_t76123gfet73g t507 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t506) + 8));
            gm_tarq9lp8tdk6f t508;
            GM_ST(uint8_t, (uint8_t *)&t508 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t508 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t508 + 4, 0);
            ((uint8_t *)&t508)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t508 + 8), t507);
            F.ret = t508;
            goto gm_fret;
        }
        gm_b16_8 t509 = GM_LD(gm_b16_8, (((uint8_t *)&t506) + 8));
        gm_node *pn510 = gm_find(cx, 2126215853);
        if (!pn510) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 280, 39);
        gm_ttln0d0hd70en t511;
        t511 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn510->clauses[8])(pn510->env, pn510->hctx, pn510, t509);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        int64_t t512 = gm_f_sys_unit_nrsqciq(t511);
        gm_tarq9lp8tdk6f t513;
        GM_ST(uint8_t, (uint8_t *)&t513 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t513 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t513 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t513 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t513 + 24, 0);
        ((uint8_t *)&t513)[0] = 0;
        GM_ST(int64_t, ((uint8_t *)&t513 + 8), t512);
        gm_tarq9lp8tdk6f t514 = t513;
        uint8_t t515 = ((((uint8_t *)&t514))[0] != 0);
        F.ret = t513;
        goto gm_fret;
        goto me37;
mn496:;
ma47:;
        uint8_t t517 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t517 != ((uint8_t)14ULL)) goto mn516;
        void * t518 = gm_l0;
        gm_tl874t2atnlh3 t519 = gm_l1;
        uint64_t t520 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t521 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t520 >= (uintptr_t)(t521)) gm_trap_index(t520, t521, gm_srcn3jjc71a2bv3, 281, 75);
        gm_b16_8 t522 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t520 * 16));
        gm_b16_8 t523 = t522;
        gm_b16_8 t524 = GM_LD(gm_b16_8, ((uint8_t *)&t523));
        gm_td0ept12e7q8b t525;
        gm_f_sys_src_5svsr6i((void *)&t525, (void *)NULL, cx, NULL, t518, t519, t524);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_td0ept12e7q8b t526 = t525;
        if (((((uint8_t *)&t526))[0] != 0)) {
            gm_t76123gfet73g t527 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t526) + 8));
            gm_tarq9lp8tdk6f t528;
            GM_ST(uint8_t, (uint8_t *)&t528 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t528 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t528 + 4, 0);
            ((uint8_t *)&t528)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t528 + 8), t527);
            F.ret = t528;
            goto gm_fret;
        }
        gm_b16_8 t529 = GM_LD(gm_b16_8, (((uint8_t *)&t526) + 8));
        gm_node *pn530 = gm_find(cx, 2126215853);
        if (!pn530) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 281, 41);
        gm_ttln0d0hd70en t531;
        t531 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn530->clauses[9])(pn530->env, pn530->hctx, pn530, t529);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        int64_t t532 = gm_f_sys_unit_nrsqciq(t531);
        gm_tarq9lp8tdk6f t533;
        GM_ST(uint8_t, (uint8_t *)&t533 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t533 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t533 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t533 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t533 + 24, 0);
        ((uint8_t *)&t533)[0] = 0;
        GM_ST(int64_t, ((uint8_t *)&t533 + 8), t532);
        gm_tarq9lp8tdk6f t534 = t533;
        uint8_t t535 = ((((uint8_t *)&t534))[0] != 0);
        F.ret = t533;
        goto gm_fret;
        goto me37;
mn516:;
ma48:;
        uint8_t t537 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t537 != ((uint8_t)15ULL)) goto mn536;
        {
            void * t539 = gm_l0;
            gm_tl874t2atnlh3 t540 = gm_l1;
            uint64_t t541 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t542 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t541 >= (uintptr_t)(t542)) gm_trap_index(t541, t542, gm_srcn3jjc71a2bv3, 283, 41);
            gm_b16_8 t543 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t541 * 16));
            gm_b16_8 t544 = t543;
            gm_b16_8 t545 = GM_LD(gm_b16_8, ((uint8_t *)&t544));
            gm_td0ept12e7q8b t546;
            gm_f_sys_src_5svsr6i((void *)&t546, (void *)NULL, cx, NULL, t539, t540, t545);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t547 = t546;
            if (((((uint8_t *)&t547))[0] != 0)) {
                gm_t76123gfet73g t548 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t547) + 8));
                gm_tarq9lp8tdk6f t549;
                GM_ST(uint8_t, (uint8_t *)&t549 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t549 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t549 + 4, 0);
                ((uint8_t *)&t549)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t549 + 8), t548);
                F.ret = t549;
                goto gm_fret;
            }
            gm_b16_8 t550 = GM_LD(gm_b16_8, (((uint8_t *)&t547) + 8));
            gm_l36 = t550;
        }
        {
            gm_b16_8 t551 = gm_l36;
            gm_node *pn552 = gm_find(cx, 2126215853);
            if (!pn552) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 284, 18);
            gm_t62ebbi2i5rkd t553;
            t553 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn552->clauses[10])(pn552->env, pn552->hctx, pn552, t551);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t62ebbi2i5rkd t554 = t553;
            gm_tm98ap4112hee t555;
            if (!((((uint8_t *)&t554))[0] != 0)) {
                gm_tm98ap4112hee t556 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t554) + 8));
                t555 = t556;
            } else {
                gm_toe5fmr3vvf4d t557 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t554) + 8));
                gm_l37 = t557;
                gm_toe5fmr3vvf4d t558 = gm_l37;
                int64_t t559 = gm_f_sys_code_cjtlfo1(t558);
                gm_tarq9lp8tdk6f t560;
                GM_ST(uint8_t, (uint8_t *)&t560 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t560 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t560 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t560 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t560 + 24, 0);
                ((uint8_t *)&t560)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t560 + 8), t559);
                gm_tarq9lp8tdk6f t561 = t560;
                uint8_t t562 = ((((uint8_t *)&t561))[0] != 0);
                F.ret = t560;
                goto gm_fret;
            }
            gm_l38 = t555;
        }
        {
            void * t563 = gm_l0;
            void * t564 = (void *)(((uint8_t *)t563) + 1352);
            int64_t t565 = gm_f_sys_next_handle_r35ek81(t564);
            gm_l39 = t565;
        }
        {
            void * t566 = gm_l0;
            void * t567 = (void *)((((uint8_t *)t566) + 1352) + 56);
            int64_t t568 = gm_l39;
            gm_tm98ap4112hee t569 = gm_l38;
            gm_tftb1odc739t5 t570 = gm_f_put_0c6jnkb(t567, t568, GM_CONV(gm_csfq64m1m1rt6m, t569));
            gm_tftb1odc739t5 t571 = t570;
            if (((((uint8_t *)&t571))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t571) + 1, gm_srcn3jjc71a2bv3, 286, 13);
            gm_b0_1 t572 = GM_LD(gm_b0_1, (((uint8_t *)&t571) + 1));
        }
        {
            int64_t t573 = gm_l39;
            gm_tarq9lp8tdk6f t574;
            GM_ST(uint8_t, (uint8_t *)&t574 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t574 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t574 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t574 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t574 + 24, 0);
            ((uint8_t *)&t574)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t574 + 8), t573);
            gm_tarq9lp8tdk6f t575 = t574;
            uint8_t t576 = ((((uint8_t *)&t575))[0] != 0);
            F.ret = t574;
            goto gm_fret;
        }
        goto me37;
mn536:;
ma49:;
        uint8_t t578 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t578 != ((uint8_t)16ULL)) goto mn577;
        {
            void * t580 = gm_l0;
            void * t581 = (void *)((((uint8_t *)t580) + 1352) + 56);
            int64_t t582 = gm_l5;
            gm_tppo2hmut88dq t583 = gm_f_get_v07up09(t581, t582);
            gm_tppo2hmut88dq t584 = t583;
            gm_tm98ap4112hee t585;
            if (((((uint8_t *)&t584))[8] != 0)) {
                gm_tm98ap4112hee t586 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t584) + 0));
                t585 = t586;
            } else {
                int64_t t587 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t588;
                GM_ST(uint8_t, (uint8_t *)&t588 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t588 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t588 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t588 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t588 + 24, 0);
                ((uint8_t *)&t588)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t588 + 8), t587);
                gm_tarq9lp8tdk6f t589 = t588;
                uint8_t t590 = ((((uint8_t *)&t589))[0] != 0);
                F.ret = t588;
                goto gm_fret;
            }
            gm_l40 = t585;
        }
        {
            void * t591 = gm_l0;
            gm_tl874t2atnlh3 t592 = gm_l1;
            uint64_t t593 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t594 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t593 >= (uintptr_t)(t594)) gm_trap_index(t593, t594, gm_srcn3jjc71a2bv3, 291, 41);
            gm_b16_8 t595 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t593 * 16));
            gm_b16_8 t596 = t595;
            gm_b16_8 t597 = GM_LD(gm_b16_8, ((uint8_t *)&t596));
            gm_td0ept12e7q8b t598;
            gm_f_sys_dest_rtrb14h((void *)&t598, (void *)NULL, cx, NULL, t591, t592, t597);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t599 = t598;
            if (((((uint8_t *)&t599))[0] != 0)) {
                gm_t76123gfet73g t600 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t599) + 8));
                gm_tarq9lp8tdk6f t601;
                GM_ST(uint8_t, (uint8_t *)&t601 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t601 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t601 + 4, 0);
                ((uint8_t *)&t601)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t601 + 8), t600);
                F.ret = t601;
                goto gm_fret;
            }
            gm_b16_8 t602 = GM_LD(gm_b16_8, (((uint8_t *)&t599) + 8));
            gm_l41 = t602;
        }
        {
            gm_tm98ap4112hee t603 = gm_l40;
            gm_b16_8 t604 = gm_l41;
            gm_node *pn605 = gm_find(cx, 2126215853);
            if (!pn605) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 292, 21);
            gm_tiuq7k5hm0kak t606;
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn605->clauses[11])((void *)&t606, pn605->env, pn605->hctx, pn605, t603, t604);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tiuq7k5hm0kak t607 = t606;
            gm_t11t9d8s5diq9 t608;
            if (!((((uint8_t *)&t607))[0] != 0)) {
                gm_t11t9d8s5diq9 t609 = GM_LD(gm_t11t9d8s5diq9, (((uint8_t *)&t607) + 8));
                t608 = t609;
            } else {
                gm_toe5fmr3vvf4d t610 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t607) + 8));
                gm_l42 = t610;
                gm_toe5fmr3vvf4d t611 = gm_l42;
                int64_t t612 = gm_f_sys_code_cjtlfo1(t611);
                gm_tarq9lp8tdk6f t613;
                GM_ST(uint8_t, (uint8_t *)&t613 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t613 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t613 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t613 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t613 + 24, 0);
                ((uint8_t *)&t613)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t613 + 8), t612);
                gm_tarq9lp8tdk6f t614 = t613;
                uint8_t t615 = ((((uint8_t *)&t614))[0] != 0);
                F.ret = t613;
                goto gm_fret;
            }
            gm_l43 = t608;
        }
        {
            gm_t11t9d8s5diq9 t616 = gm_l43;
            gm_t11t9d8s5diq9 t617 = t616;
            gm_t617vr72bd7b3 t618;
            if (((((uint8_t *)&t617))[16] != 0)) {
                gm_t617vr72bd7b3 t619 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t617) + 0));
                t618 = t619;
            } else {
                gm_tarq9lp8tdk6f t620;
                GM_ST(uint8_t, (uint8_t *)&t620 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t620 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t620 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t620 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t620 + 24, 0);
                ((uint8_t *)&t620)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t620 + 8), ((int64_t)0LL));
                gm_tarq9lp8tdk6f t621 = t620;
                uint8_t t622 = ((((uint8_t *)&t621))[0] != 0);
                F.ret = t620;
                goto gm_fret;
            }
            gm_l44 = t618;
        }
        {
            uint64_t t623 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 0));
            int64_t t624 = (int64_t)t623;
            gm_t9qkh0rsooc8d t625 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l44) + 8));
            int64_t t626 = gm_f_sys_kind_sjfdqbt(t625);
            unsigned __int128 n627 = (unsigned __int128)((uint64_t)32ULL);
            if (n627 >= 64) gm_trap_shift((uint64_t)n627, "i64", gm_srcn3jjc71a2bv3, 294, 44);
            int64_t r628 = (int64_t)((uint64_t)t626 << (int)n627);
            bool o629 = (int64_t)(r628 >> (int)n627) != t626;
            if (o629) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 294, 44);
            int64_t t630 = r628;
            int64_t r631; bool o632 = __builtin_add_overflow(t624, t630, &r631);
            if (o632) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 294, 20);
            int64_t t633 = r631;
            gm_tarq9lp8tdk6f t634;
            GM_ST(uint8_t, (uint8_t *)&t634 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t634 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t634 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t634 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t634 + 24, 0);
            ((uint8_t *)&t634)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t634 + 8), t633);
            gm_tarq9lp8tdk6f t635 = t634;
            uint8_t t636 = ((((uint8_t *)&t635))[0] != 0);
            F.ret = t634;
            goto gm_fret;
        }
        goto me37;
mn577:;
ma50:;
        uint8_t t638 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t638 != ((uint8_t)17ULL)) goto mn637;
        {
            void * t640 = gm_l0;
            void * t641 = (void *)((((uint8_t *)t640) + 1352) + 56);
            int64_t t642 = gm_l5;
            gm_tppo2hmut88dq t643 = gm_f_remove_20jm5n8(t641, t642);
            gm_tppo2hmut88dq t644 = t643;
            gm_tm98ap4112hee t645;
            if (((((uint8_t *)&t644))[8] != 0)) {
                gm_tm98ap4112hee t646 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t644) + 0));
                t645 = t646;
            } else {
                int64_t t647 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t648;
                GM_ST(uint8_t, (uint8_t *)&t648 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t648 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t648 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t648 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t648 + 24, 0);
                ((uint8_t *)&t648)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t648 + 8), t647);
                gm_tarq9lp8tdk6f t649 = t648;
                uint8_t t650 = ((((uint8_t *)&t649))[0] != 0);
                F.ret = t648;
                goto gm_fret;
            }
            gm_l45 = t645;
        }
        {
            gm_tm98ap4112hee t651 = gm_l45;
            gm_node *pn652 = gm_find(cx, 2126215853);
            if (!pn652) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 298, 13);
            gm_b0_1 t653;
            t653 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn652->clauses[12])(pn652->env, pn652->hctx, pn652, t651);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tarq9lp8tdk6f t654;
            GM_ST(uint8_t, (uint8_t *)&t654 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t654 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t654 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t654 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t654 + 24, 0);
            ((uint8_t *)&t654)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t654 + 8), ((int64_t)0LL));
            gm_tarq9lp8tdk6f t655 = t654;
            uint8_t t656 = ((((uint8_t *)&t655))[0] != 0);
            F.ret = t654;
            goto gm_fret;
        }
        goto me37;
mn637:;
ma51:;
        uint8_t t658 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t658 != ((uint8_t)19ULL)) goto mn657;
        {
            void * t660 = gm_l0;
            uint64_t t661 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t662 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t661 >= (uintptr_t)(t662)) gm_trap_index(t661, t662, gm_srcn3jjc71a2bv3, 302, 32);
            gm_b16_8 t663 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t661 * 16));
            gm_b16_8 t664 = t663;
            gm_b16_8 t665 = GM_LD(gm_b16_8, ((uint8_t *)&t664));
            unsigned __int128 t666 = gm_f_get_uint_bq8c4b7(t660, t665);
            uint64_t t667 = (uint64_t)t666;
            gm_l46 = t667;
        }
        {
            void * t668 = gm_l0;
            void * t669 = (void *)((((uint8_t *)t668) + 1352) + 0);
            int64_t t670 = gm_l5;
            gm_tppo2hmut88dq t671 = gm_f_get_f2mbmm8(t669, t670);
            gm_tppo2hmut88dq t672 = t671;
            gm_tm98ap4112hee t673;
            if (((((uint8_t *)&t672))[8] != 0)) {
                gm_tm98ap4112hee t674 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t672) + 0));
                t673 = t674;
            } else {
                int64_t t675 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t676;
                GM_ST(uint8_t, (uint8_t *)&t676 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t676 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t676 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t676 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t676 + 24, 0);
                ((uint8_t *)&t676)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t676 + 8), t675);
                gm_tarq9lp8tdk6f t677 = t676;
                uint8_t t678 = ((((uint8_t *)&t677))[0] != 0);
                F.ret = t676;
                goto gm_fret;
            }
            gm_l47 = t673;
        }
        {
            gm_tm98ap4112hee t679 = gm_l47;
            uint64_t t680 = gm_l46;
            gm_node *pn681 = gm_find(cx, 2126215853);
            if (!pn681) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 304, 29);
            gm_ttln0d0hd70en t682;
            t682 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, uint64_t))pn681->clauses[14])(pn681->env, pn681->hctx, pn681, t679, t680);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t683 = gm_f_sys_unit_nrsqciq(t682);
            gm_tarq9lp8tdk6f t684;
            GM_ST(uint8_t, (uint8_t *)&t684 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t684 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t684 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t684 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t684 + 24, 0);
            ((uint8_t *)&t684)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t684 + 8), t683);
            gm_tarq9lp8tdk6f t685 = t684;
            uint8_t t686 = ((((uint8_t *)&t685))[0] != 0);
            F.ret = t684;
            goto gm_fret;
        }
        goto me37;
mn657:;
ma52:;
        uint8_t t688 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t688 != ((uint8_t)20ULL)) goto mn687;
        {
            void * t690 = gm_l0;
            void * t691 = (void *)((((uint8_t *)t690) + 1352) + 0);
            int64_t t692 = gm_l5;
            gm_tppo2hmut88dq t693 = gm_f_get_f2mbmm8(t691, t692);
            gm_tppo2hmut88dq t694 = t693;
            gm_tm98ap4112hee t695;
            if (((((uint8_t *)&t694))[8] != 0)) {
                gm_tm98ap4112hee t696 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t694) + 0));
                t695 = t696;
            } else {
                int64_t t697 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t698;
                GM_ST(uint8_t, (uint8_t *)&t698 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t698 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t698 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t698 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t698 + 24, 0);
                ((uint8_t *)&t698)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t698 + 8), t697);
                gm_tarq9lp8tdk6f t699 = t698;
                uint8_t t700 = ((((uint8_t *)&t699))[0] != 0);
                F.ret = t698;
                goto gm_fret;
            }
            gm_l48 = t695;
        }
        {
            gm_tm98ap4112hee t701 = gm_l48;
            gm_node *pn702 = gm_find(cx, 2126215853);
            if (!pn702) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 308, 29);
            gm_ttln0d0hd70en t703;
            t703 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn702->clauses[15])(pn702->env, pn702->hctx, pn702, t701);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t704 = gm_f_sys_unit_nrsqciq(t703);
            gm_tarq9lp8tdk6f t705;
            GM_ST(uint8_t, (uint8_t *)&t705 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t705 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t705 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t705 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t705 + 24, 0);
            ((uint8_t *)&t705)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t705 + 8), t704);
            gm_tarq9lp8tdk6f t706 = t705;
            uint8_t t707 = ((((uint8_t *)&t706))[0] != 0);
            F.ret = t705;
            goto gm_fret;
        }
        goto me37;
mn687:;
ma53:;
        uint8_t t709 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t709 != ((uint8_t)21ULL)) goto mn708;
        {
            void * t711 = gm_l0;
            gm_tl874t2atnlh3 t712 = gm_l1;
            uint64_t t713 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t714 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t713 >= (uintptr_t)(t714)) gm_trap_index(t713, t714, gm_srcn3jjc71a2bv3, 311, 41);
            gm_b16_8 t715 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t713 * 16));
            gm_b16_8 t716 = t715;
            gm_b16_8 t717 = GM_LD(gm_b16_8, ((uint8_t *)&t716));
            gm_td0ept12e7q8b t718;
            gm_f_sys_dest_rtrb14h((void *)&t718, (void *)NULL, cx, NULL, t711, t712, t717);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t719 = t718;
            if (((((uint8_t *)&t719))[0] != 0)) {
                gm_t76123gfet73g t720 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t719) + 8));
                gm_tarq9lp8tdk6f t721;
                GM_ST(uint8_t, (uint8_t *)&t721 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t721 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t721 + 4, 0);
                ((uint8_t *)&t721)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t721 + 8), t720);
                F.ret = t721;
                goto gm_fret;
            }
            gm_b16_8 t722 = GM_LD(gm_b16_8, (((uint8_t *)&t719) + 8));
            gm_l49 = t722;
        }
        {
            gm_b16_8 t723 = gm_l49;
            gm_node *pn724 = gm_find(cx, 2126215853);
            if (!pn724) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 312, 30);
            gm_thf5kr0is9u9i t725;
            t725 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn724->clauses[16])(pn724->env, pn724->hctx, pn724, t723);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t726 = gm_f_sys_count_50c5lcp(t725);
            gm_tarq9lp8tdk6f t727;
            GM_ST(uint8_t, (uint8_t *)&t727 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t727 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t727 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t727 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t727 + 24, 0);
            ((uint8_t *)&t727)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t727 + 8), t726);
            gm_tarq9lp8tdk6f t728 = t727;
            uint8_t t729 = ((((uint8_t *)&t728))[0] != 0);
            F.ret = t727;
            goto gm_fret;
        }
        goto me37;
mn708:;
ma54:;
        uint8_t t731 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t731 != ((uint8_t)22ULL)) goto mn730;
        {
            void * t733 = gm_l0;
            gm_tl874t2atnlh3 t734 = gm_l1;
            uint64_t t735 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t736 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t735 >= (uintptr_t)(t736)) gm_trap_index(t735, t736, gm_srcn3jjc71a2bv3, 315, 41);
            gm_b16_8 t737 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t735 * 16));
            gm_b16_8 t738 = t737;
            gm_b16_8 t739 = GM_LD(gm_b16_8, ((uint8_t *)&t738));
            gm_td0ept12e7q8b t740;
            gm_f_sys_src_5svsr6i((void *)&t740, (void *)NULL, cx, NULL, t733, t734, t739);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t741 = t740;
            if (((((uint8_t *)&t741))[0] != 0)) {
                gm_t76123gfet73g t742 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t741) + 8));
                gm_tarq9lp8tdk6f t743;
                GM_ST(uint8_t, (uint8_t *)&t743 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t743 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t743 + 4, 0);
                ((uint8_t *)&t743)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t743 + 8), t742);
                F.ret = t743;
                goto gm_fret;
            }
            gm_b16_8 t744 = GM_LD(gm_b16_8, (((uint8_t *)&t741) + 8));
            gm_l50 = t744;
        }
        {
            void * t745 = gm_l0;
            gm_tl874t2atnlh3 t746 = gm_l1;
            uint64_t t747 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t748 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t747 >= (uintptr_t)(t748)) gm_trap_index(t747, t748, gm_srcn3jjc71a2bv3, 316, 41);
            gm_b16_8 t749 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t747 * 16));
            gm_b16_8 t750 = t749;
            gm_b16_8 t751 = GM_LD(gm_b16_8, ((uint8_t *)&t750));
            gm_td0ept12e7q8b t752;
            gm_f_sys_dest_rtrb14h((void *)&t752, (void *)NULL, cx, NULL, t745, t746, t751);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t753 = t752;
            if (((((uint8_t *)&t753))[0] != 0)) {
                gm_t76123gfet73g t754 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t753) + 8));
                gm_tarq9lp8tdk6f t755;
                GM_ST(uint8_t, (uint8_t *)&t755 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t755 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t755 + 4, 0);
                ((uint8_t *)&t755)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t755 + 8), t754);
                F.ret = t755;
                goto gm_fret;
            }
            gm_b16_8 t756 = GM_LD(gm_b16_8, (((uint8_t *)&t753) + 8));
            gm_l51 = t756;
        }
        {
            gm_b16_8 t757 = gm_l50;
            gm_b16_8 t758 = gm_l51;
            gm_node *pn759 = gm_find(cx, 2126215853);
            if (!pn759) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 317, 30);
            gm_thf5kr0is9u9i t760;
            t760 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn759->clauses[17])(pn759->env, pn759->hctx, pn759, t757, t758);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t761 = gm_f_sys_count_50c5lcp(t760);
            gm_tarq9lp8tdk6f t762;
            GM_ST(uint8_t, (uint8_t *)&t762 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t762 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t762 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t762 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t762 + 24, 0);
            ((uint8_t *)&t762)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t762 + 8), t761);
            gm_tarq9lp8tdk6f t763 = t762;
            uint8_t t764 = ((((uint8_t *)&t763))[0] != 0);
            F.ret = t762;
            goto gm_fret;
        }
        goto me37;
mn730:;
ma55:;
        uint8_t t766 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t766 != ((uint8_t)23ULL)) goto mn765;
        {
            void * t768 = gm_l0;
            gm_tl874t2atnlh3 t769 = gm_l1;
            uint64_t t770 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t771 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t770 >= (uintptr_t)(t771)) gm_trap_index(t770, t771, gm_srcn3jjc71a2bv3, 320, 41);
            gm_b16_8 t772 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t770 * 16));
            gm_b16_8 t773 = t772;
            gm_b16_8 t774 = GM_LD(gm_b16_8, ((uint8_t *)&t773));
            gm_td0ept12e7q8b t775;
            gm_f_sys_src_5svsr6i((void *)&t775, (void *)NULL, cx, NULL, t768, t769, t774);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t776 = t775;
            if (((((uint8_t *)&t776))[0] != 0)) {
                gm_t76123gfet73g t777 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t776) + 8));
                gm_tarq9lp8tdk6f t778;
                GM_ST(uint8_t, (uint8_t *)&t778 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t778 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t778 + 4, 0);
                ((uint8_t *)&t778)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t778 + 8), t777);
                F.ret = t778;
                goto gm_fret;
            }
            gm_b16_8 t779 = GM_LD(gm_b16_8, (((uint8_t *)&t776) + 8));
            gm_l52 = t779;
        }
        {
            void * t780 = gm_l0;
            gm_tl874t2atnlh3 t781 = gm_l1;
            uint64_t t782 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t783 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t782 >= (uintptr_t)(t783)) gm_trap_index(t782, t783, gm_srcn3jjc71a2bv3, 321, 41);
            gm_b16_8 t784 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t782 * 16));
            gm_b16_8 t785 = t784;
            gm_b16_8 t786 = GM_LD(gm_b16_8, ((uint8_t *)&t785));
            gm_td0ept12e7q8b t787;
            gm_f_sys_dest_rtrb14h((void *)&t787, (void *)NULL, cx, NULL, t780, t781, t786);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t788 = t787;
            if (((((uint8_t *)&t788))[0] != 0)) {
                gm_t76123gfet73g t789 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t788) + 8));
                gm_tarq9lp8tdk6f t790;
                GM_ST(uint8_t, (uint8_t *)&t790 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t790 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t790 + 4, 0);
                ((uint8_t *)&t790)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t790 + 8), t789);
                F.ret = t790;
                goto gm_fret;
            }
            gm_b16_8 t791 = GM_LD(gm_b16_8, (((uint8_t *)&t788) + 8));
            gm_l53 = t791;
        }
        {
            gm_b16_8 t792 = gm_l52;
            gm_b16_8 t793 = gm_l53;
            gm_node *pn794 = gm_find(cx, 2126215853);
            if (!pn794) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 322, 30);
            gm_thf5kr0is9u9i t795;
            t795 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn794->clauses[18])(pn794->env, pn794->hctx, pn794, t792, t793);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t796 = gm_f_sys_count_50c5lcp(t795);
            gm_tarq9lp8tdk6f t797;
            GM_ST(uint8_t, (uint8_t *)&t797 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t797 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t797 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t797 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t797 + 24, 0);
            ((uint8_t *)&t797)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t797 + 8), t796);
            gm_tarq9lp8tdk6f t798 = t797;
            uint8_t t799 = ((((uint8_t *)&t798))[0] != 0);
            F.ret = t797;
            goto gm_fret;
        }
        goto me37;
mn765:;
ma56:;
        uint8_t t801 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t801 != ((uint8_t)24ULL)) goto mn800;
        {
            void * t803 = gm_l0;
            gm_tl874t2atnlh3 t804 = gm_l1;
            uint64_t t805 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t806 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t805 >= (uintptr_t)(t806)) gm_trap_index(t805, t806, gm_srcn3jjc71a2bv3, 325, 43);
            gm_b16_8 t807 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t805 * 16));
            gm_b16_8 t808 = t807;
            gm_b16_8 t809 = GM_LD(gm_b16_8, ((uint8_t *)&t808));
            gm_td0ept12e7q8b t810;
            gm_f_sys_src_5svsr6i((void *)&t810, (void *)NULL, cx, NULL, t803, t804, t809);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t811 = t810;
            if (((((uint8_t *)&t811))[0] != 0)) {
                gm_t76123gfet73g t812 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t811) + 8));
                gm_tarq9lp8tdk6f t813;
                GM_ST(uint8_t, (uint8_t *)&t813 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t813 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t813 + 4, 0);
                ((uint8_t *)&t813)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t813 + 8), t812);
                F.ret = t813;
                goto gm_fret;
            }
            gm_b16_8 t814 = GM_LD(gm_b16_8, (((uint8_t *)&t811) + 8));
            gm_l54 = t814;
        }
        {
            void * t815 = gm_l0;
            gm_tl874t2atnlh3 t816 = gm_l1;
            uint64_t t817 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t818 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t817 >= (uintptr_t)(t818)) gm_trap_index(t817, t818, gm_srcn3jjc71a2bv3, 326, 41);
            gm_b16_8 t819 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t817 * 16));
            gm_b16_8 t820 = t819;
            gm_b16_8 t821 = GM_LD(gm_b16_8, ((uint8_t *)&t820));
            gm_td0ept12e7q8b t822;
            gm_f_sys_src_5svsr6i((void *)&t822, (void *)NULL, cx, NULL, t815, t816, t821);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t823 = t822;
            if (((((uint8_t *)&t823))[0] != 0)) {
                gm_t76123gfet73g t824 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t823) + 8));
                gm_tarq9lp8tdk6f t825;
                GM_ST(uint8_t, (uint8_t *)&t825 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t825 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t825 + 4, 0);
                ((uint8_t *)&t825)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t825 + 8), t824);
                F.ret = t825;
                goto gm_fret;
            }
            gm_b16_8 t826 = GM_LD(gm_b16_8, (((uint8_t *)&t823) + 8));
            gm_l55 = t826;
        }
        {
            gm_b16_8 t827 = gm_l54;
            gm_b16_8 t828 = gm_l55;
            gm_node *pn829 = gm_find(cx, 2126215853);
            if (!pn829) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 327, 29);
            gm_ttln0d0hd70en t830;
            t830 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn829->clauses[19])(pn829->env, pn829->hctx, pn829, t827, t828);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t831 = gm_f_sys_unit_nrsqciq(t830);
            gm_tarq9lp8tdk6f t832;
            GM_ST(uint8_t, (uint8_t *)&t832 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t832 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t832 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t832 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t832 + 24, 0);
            ((uint8_t *)&t832)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t832 + 8), t831);
            gm_tarq9lp8tdk6f t833 = t832;
            uint8_t t834 = ((((uint8_t *)&t833))[0] != 0);
            F.ret = t832;
            goto gm_fret;
        }
        goto me37;
mn800:;
ma57:;
        uint8_t t836 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t836 != ((uint8_t)25ULL)) goto mn835;
        {
            void * t838 = gm_l0;
            gm_tl874t2atnlh3 t839 = gm_l1;
            uint64_t t840 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t841 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t840 >= (uintptr_t)(t841)) gm_trap_index(t840, t841, gm_srcn3jjc71a2bv3, 330, 41);
            gm_b16_8 t842 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t840 * 16));
            gm_b16_8 t843 = t842;
            gm_b16_8 t844 = GM_LD(gm_b16_8, ((uint8_t *)&t843));
            gm_td0ept12e7q8b t845;
            gm_f_sys_src_5svsr6i((void *)&t845, (void *)NULL, cx, NULL, t838, t839, t844);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t846 = t845;
            if (((((uint8_t *)&t846))[0] != 0)) {
                gm_t76123gfet73g t847 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t846) + 8));
                gm_tarq9lp8tdk6f t848;
                GM_ST(uint8_t, (uint8_t *)&t848 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t848 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t848 + 4, 0);
                ((uint8_t *)&t848)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t848 + 8), t847);
                F.ret = t848;
                goto gm_fret;
            }
            gm_b16_8 t849 = GM_LD(gm_b16_8, (((uint8_t *)&t846) + 8));
            gm_l56 = t849;
        }
        {
            gm_b16_8 t850 = gm_l56;
            void * t851 = gm_l0;
            uint64_t t852 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t853 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t852 >= (uintptr_t)(t853)) gm_trap_index(t852, t853, gm_srcn3jjc71a2bv3, 331, 58);
            gm_b16_8 t854 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t852 * 16));
            gm_b16_8 t855 = t854;
            gm_b16_8 t856 = GM_LD(gm_b16_8, ((uint8_t *)&t855));
            unsigned __int128 t857 = gm_f_get_uint_bq8c4b7(t851, t856);
            uint32_t t858 = (uint32_t)t857;
            gm_node *pn859 = gm_find(cx, 2126215853);
            if (!pn859) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 331, 29);
            gm_ttln0d0hd70en t860;
            t860 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, uint32_t))pn859->clauses[20])(pn859->env, pn859->hctx, pn859, t850, t858);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t861 = gm_f_sys_unit_nrsqciq(t860);
            gm_tarq9lp8tdk6f t862;
            GM_ST(uint8_t, (uint8_t *)&t862 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t862 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t862 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t862 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t862 + 24, 0);
            ((uint8_t *)&t862)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t862 + 8), t861);
            gm_tarq9lp8tdk6f t863 = t862;
            uint8_t t864 = ((((uint8_t *)&t863))[0] != 0);
            F.ret = t862;
            goto gm_fret;
        }
        goto me37;
mn835:;
ma58:;
        uint8_t t866 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t866 != ((uint8_t)26ULL)) goto mn865;
        {
            void * t868 = gm_l0;
            gm_tl874t2atnlh3 t869 = gm_l1;
            uint64_t t870 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t871 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t870 >= (uintptr_t)(t871)) gm_trap_index(t870, t871, gm_srcn3jjc71a2bv3, 334, 41);
            gm_b16_8 t872 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t870 * 16));
            gm_b16_8 t873 = t872;
            gm_b16_8 t874 = GM_LD(gm_b16_8, ((uint8_t *)&t873));
            gm_td0ept12e7q8b t875;
            gm_f_sys_src_5svsr6i((void *)&t875, (void *)NULL, cx, NULL, t868, t869, t874);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t876 = t875;
            if (((((uint8_t *)&t876))[0] != 0)) {
                gm_t76123gfet73g t877 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t876) + 8));
                gm_tarq9lp8tdk6f t878;
                GM_ST(uint8_t, (uint8_t *)&t878 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t878 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t878 + 4, 0);
                ((uint8_t *)&t878)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t878 + 8), t877);
                F.ret = t878;
                goto gm_fret;
            }
            gm_b16_8 t879 = GM_LD(gm_b16_8, (((uint8_t *)&t876) + 8));
            gm_l57 = t879;
        }
        {
            gm_b16_8 t880 = gm_l57;
            void * t881 = gm_l0;
            uint64_t t882 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t883 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t882 >= (uintptr_t)(t883)) gm_trap_index(t882, t883, gm_srcn3jjc71a2bv3, 335, 62);
            void * t884 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t882 * 8));
            gm_t88rcucg1b7uk t885 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t884) + 96));
            uint64_t t886 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t887 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t886 >= (uintptr_t)(t887)) gm_trap_index(t886, t887, gm_srcn3jjc71a2bv3, 335, 74);
            gm_b16_8 t888 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t886 * 16));
            gm_b16_8 t889 = t888;
            gm_b16_8 t890 = GM_LD(gm_b16_8, ((uint8_t *)&t889));
            int64_t t891 = gm_f_sys_int_32e79ed(t881, GM_CONV(gm_csguankko4lmhd, t885), t890);
            gm_node *pn892 = gm_find(cx, 2126215853);
            if (!pn892) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 335, 29);
            gm_ttln0d0hd70en t893;
            t893 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, int64_t))pn892->clauses[21])(pn892->env, pn892->hctx, pn892, t880, t891);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t894 = gm_f_sys_unit_nrsqciq(t893);
            gm_tarq9lp8tdk6f t895;
            GM_ST(uint8_t, (uint8_t *)&t895 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t895 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t895 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t895 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t895 + 24, 0);
            ((uint8_t *)&t895)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t895 + 8), t894);
            gm_tarq9lp8tdk6f t896 = t895;
            uint8_t t897 = ((((uint8_t *)&t896))[0] != 0);
            F.ret = t895;
            goto gm_fret;
        }
        goto me37;
mn865:;
ma59:;
        void * t899 = gm_l0;
        gm_tl874t2atnlh3 t900 = gm_l1;
        gm_b16_8 t901;
        GM_ST(void *, (uint8_t *)&t901, (void *)(gm_sqn7kgtu67hrg));
        GM_ST(uintptr_t, (uint8_t *)&t901 + 8, (uintptr_t)(41));
        gm_t76123gfet73g t902;
        gm_f_trap_qh84l9d((void *)&t902, (void *)NULL, cx, NULL, t899, t900, t901);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tarq9lp8tdk6f t903;
        GM_ST(uint8_t, (uint8_t *)&t903 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t903 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t903 + 4, 0);
        ((uint8_t *)&t903)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t903 + 8), t902);
        gm_tarq9lp8tdk6f t904 = t903;
        uint8_t t905 = ((((uint8_t *)&t904))[0] != 0);
        F.ret = t903;
        goto gm_fret;
        goto me37;
mn898:;
ma60:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 217, 5);
me37:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_sys_has_0sego5l(uint32_t a0, uint32_t a1);
inline __attribute__((aligned(4))) int64_t gm_f_sys_int_32e79ed(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_int_32e79ed F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* b */
    __int128 gm_l3; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 197u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_l2;
        unsigned __int128 t5 = gm_f_get_int_pg7p4gc(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
        unsigned __int128 t6 = t5;
        __int128 t7;
        memset(&t7, 0, sizeof t7); memcpy(&t7, ((uint8_t *)&t6), sizeof t7 < 16 ? sizeof t7 : 16);
        gm_l3 = t7;
    }
    {
        uint8_t t9 = 0;
        __int128 t10 = gm_l3;
        uint8_t t11 = (t10 < ((__int128)(((unsigned __int128)18446744073709551615u << 64) | 9223372036854775808u)));
        if (!t11) {
            __int128 t12 = gm_l3;
            uint8_t t13 = (t12 > ((__int128)(((unsigned __int128)0u << 64) | 9223372036854775807u)));
            t9 = t13;
        } else t9 = 1;
        if (!t9) goto ie8;
        {
            uint8_t t15 = 0;
            F.ret = ((int64_t)-1LL);
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        __int128 t16 = gm_l3;
        int64_t t17 = (int64_t)t16;
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) int64_t gm_f_sys_kind_sjfdqbt(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_sys_net_aff7h5l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sys_net_aff7h5l F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    int64_t gm_l5; /* h */
    gm_toe5fmr3vvf4d gm_l9; /* err */
    gm_tm98ap4112hee gm_l10; /* s */
    int64_t gm_l11; /* handle */
    gm_toe5fmr3vvf4d gm_l15; /* err */
    gm_tm98ap4112hee gm_l16; /* l */
    int64_t gm_l17; /* handle */
    gm_toe5fmr3vvf4d gm_l20; /* err */
    uint16_t gm_l21; /* port */
    gm_toe5fmr3vvf4d gm_l24; /* err */
    gm_tm98ap4112hee gm_l25; /* s */
    int64_t gm_l26; /* handle */
    uint64_t gm_l38; /* recv_ns */
    uint64_t gm_l39; /* send_ns */
    GM_STACK_CHECK(&F, 6626u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_l5 = ((int64_t)-1LL);
    }
    {
        gm_t9qkh0rsooc8d t3 = gm_l2;
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)27ULL));
        gm_t9qkh0rsooc8d t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)28ULL));
        gm_t0am46iohlpol t6;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t6 + 0), t4);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t6 + 1), t5);
        gm_t0am46iohlpol t7 = t6;
        uint8_t t8 = 0;
        gm_t9qkh0rsooc8d t9 = t3;
        if (gm_eqtje7cegoml(((uint8_t *)&t7) + 0, ((uint8_t *)&t9))) t8 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t7) + 1, ((uint8_t *)&t9))) t8 = 1;
        uint8_t t10 = (uint8_t)(!t8);
        if (!t10) goto ie2;
        {
            void * t12 = gm_l0;
            uint64_t t13 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t13 >= (uintptr_t)(t14)) gm_trap_index(t13, t14, gm_srcn3jjc71a2bv3, 344, 70);
            void * t15 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t13 * 8));
            gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t15) + 96));
            uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srcn3jjc71a2bv3, 344, 82);
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t17 * 16));
            gm_b16_8 t20 = t19;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            int64_t t22 = gm_f_sys_int_32e79ed(t12, GM_CONV(gm_csguankko4lmhd, t16), t21);
            gm_l5 = t22;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t36) { case ((uint8_t)0ULL): goto ma34; case ((uint8_t)1ULL): goto ma34; case ((uint8_t)2ULL): goto ma34; case ((uint8_t)3ULL): goto ma34; case ((uint8_t)4ULL): goto ma34; case ((uint8_t)5ULL): goto ma34; case ((uint8_t)6ULL): goto ma34; case ((uint8_t)7ULL): goto ma34; case ((uint8_t)8ULL): goto ma34; case ((uint8_t)9ULL): goto ma34; case ((uint8_t)10ULL): goto ma34; case ((uint8_t)11ULL): goto ma34; case ((uint8_t)12ULL): goto ma34; case ((uint8_t)13ULL): goto ma34; case ((uint8_t)14ULL): goto ma34; case ((uint8_t)15ULL): goto ma34; case ((uint8_t)16ULL): goto ma34; case ((uint8_t)17ULL): goto ma34; case ((uint8_t)18ULL): goto ma34; case ((uint8_t)19ULL): goto ma34; case ((uint8_t)20ULL): goto ma34; case ((uint8_t)21ULL): goto ma34; case ((uint8_t)22ULL): goto ma34; case ((uint8_t)23ULL): goto ma34; case ((uint8_t)24ULL): goto ma34; case ((uint8_t)25ULL): goto ma34; case ((uint8_t)26ULL): goto ma34; case ((uint8_t)27ULL): goto ma24; case ((uint8_t)28ULL): goto ma25; case ((uint8_t)29ULL): goto ma26; case ((uint8_t)30ULL): goto ma27; case ((uint8_t)31ULL): goto ma28; case ((uint8_t)32ULL): goto ma29; case ((uint8_t)33ULL): goto ma30; case ((uint8_t)34ULL): goto ma31; case ((uint8_t)35ULL): goto ma32; case ((uint8_t)36ULL): goto ma33; case ((uint8_t)37ULL): goto ma34; case ((uint8_t)38ULL): goto ma34; case ((uint8_t)39ULL): goto ma34; case ((uint8_t)40ULL): goto ma34; case ((uint8_t)41ULL): goto ma34; case ((uint8_t)42ULL): goto ma34; case ((uint8_t)43ULL): goto ma34; case ((uint8_t)44ULL): goto ma34; case ((uint8_t)45ULL): goto ma34; case ((uint8_t)46ULL): goto ma34; case ((uint8_t)47ULL): goto ma34; case ((uint8_t)48ULL): goto ma34; case ((uint8_t)49ULL): goto ma34; case ((uint8_t)50ULL): goto ma34; case ((uint8_t)51ULL): goto ma34; case ((uint8_t)52ULL): goto ma34; case ((uint8_t)53ULL): goto ma34; case ((uint8_t)54ULL): goto ma34; default: goto ma34; }
ma24:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t38 != ((uint8_t)27ULL)) goto mn37;
        {
            void * t40 = gm_l0;
            gm_tl874t2atnlh3 t41 = gm_l1;
            uint64_t t42 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_srcn3jjc71a2bv3, 347, 41);
            gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t42 * 16));
            gm_b16_8 t45 = t44;
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
            gm_td0ept12e7q8b t47;
            gm_f_sys_src_5svsr6i((void *)&t47, (void *)NULL, cx, NULL, t40, t41, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t48 = t47;
            if (((((uint8_t *)&t48))[0] != 0)) {
                gm_t76123gfet73g t49 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t48) + 8));
                gm_tarq9lp8tdk6f t50;
                GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                ((uint8_t *)&t50)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t50 + 8), t49);
                F.ret = t50;
                goto gm_fret;
            }
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t48) + 8));
            F.l6 = t51;
        }
        {
            void * t52 = gm_l0;
            uint64_t t53 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t53 >= (uintptr_t)(t54)) gm_trap_index(t53, t54, gm_srcn3jjc71a2bv3, 348, 32);
            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t53 * 16));
            gm_b16_8 t56 = t55;
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
            unsigned __int128 t58 = gm_f_get_uint_bq8c4b7(t52, t57);
            uint16_t t59 = (uint16_t)t58;
            F.l7 = t59;
        }
        {
            gm_toe5fmr3vvf4d t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)12ULL));
            gm_t62ebbi2i5rkd t61;
            GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
            ((uint8_t *)&t61)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t61 + 8), t60);
            F.l8 = t61;
        }
        {
            void * t63 = gm_l0;
            uint64_t t64 = GM_LD(uintptr_t, (((((uint8_t *)t63) + 1656) + 0) + 0) + 8);
            uint8_t t65 = (t64 > ((uint64_t)1ULL));
            if (!t65) goto ie62;
            {
                void * t67 = gm_l0;
                gm_tl874t2atnlh3 t68 = gm_l1;
                gm_tvaas22dl64e0 t69;
                memset((uint8_t *)&t69 + 18, 0, 6);
                gm_b16_8 t70 = F.l6;
                gm_b16_8 t71 = t70;
                gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                GM_ST(gm_b16_8, (uint8_t *)&t69 + 0, t72);
                uint16_t t73 = F.l7;
                GM_ST(uint16_t, (uint8_t *)&t69 + 16, t73);
                gm_tmvdbo28o1i6h t74;
                GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)2ULL));
                GM_ST(gm_tvaas22dl64e0, ((uint8_t *)&t74 + 8), t69);
                gm_toqti1ubjnfd1 t75;
                gm_f_socket_on_host_thread_u8c2vg5((void *)&t75, (void *)NULL, cx, NULL, t67, t68, &t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_toqti1ubjnfd1 t76 = t75;
                if (((((uint8_t *)&t76))[0] != 0)) {
                    gm_t76123gfet73g t77 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t76) + 8));
                    gm_tarq9lp8tdk6f t78;
                    GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
                    ((uint8_t *)&t78)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t78 + 8), t77);
                    F.ret = t78;
                    goto gm_fret;
                }
                gm_t62ebbi2i5rkd t79 = GM_LD(gm_t62ebbi2i5rkd, (((uint8_t *)&t76) + 8));
                F.l8 = t79;
            }
            goto ix62;
ie62:;
            {
                gm_closure t81 = ((gm_closure){(void *)gm_f_fn_aqcn8lk, (void *)&F.h, cx});
                gm_f_defaults_p077jgf(t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix62:;
        }
        {
            gm_t62ebbi2i5rkd t82 = F.l8;
            gm_t62ebbi2i5rkd t83 = t82;
            gm_tm98ap4112hee t84;
            if (!((((uint8_t *)&t83))[0] != 0)) {
                gm_tm98ap4112hee t85 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t83) + 8));
                t84 = t85;
            } else {
                gm_toe5fmr3vvf4d t86 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t83) + 8));
                gm_l9 = t86;
                gm_toe5fmr3vvf4d t87 = gm_l9;
                int64_t t88 = gm_f_sys_code_cjtlfo1(t87);
                gm_tarq9lp8tdk6f t89;
                GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t89 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t89 + 24, 0);
                ((uint8_t *)&t89)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t89 + 8), t88);
                gm_tarq9lp8tdk6f t90 = t89;
                uint8_t t91 = ((((uint8_t *)&t90))[0] != 0);
                F.ret = t89;
                goto gm_fret;
            }
            gm_l10 = t84;
        }
        {
            void * t92 = gm_l0;
            void * t93 = (void *)(((uint8_t *)t92) + 1352);
            int64_t t94 = gm_f_sys_next_handle_r35ek81(t93);
            gm_l11 = t94;
        }
        {
            void * t95 = gm_l0;
            void * t96 = (void *)((((uint8_t *)t95) + 1352) + 112);
            int64_t t97 = gm_l11;
            gm_tm98ap4112hee t98 = gm_l10;
            gm_tftb1odc739t5 t99 = gm_f_put_qkbvor7(t96, t97, GM_CONV(gm_csfq64m1m1rt6m, t98));
            gm_tftb1odc739t5 t100 = t99;
            if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_srcn3jjc71a2bv3, 357, 13);
            gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
        }
        {
            int64_t t102 = gm_l11;
            gm_tarq9lp8tdk6f t103;
            GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t103 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t103 + 24, 0);
            ((uint8_t *)&t103)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t103 + 8), t102);
            gm_tarq9lp8tdk6f t104 = t103;
            uint8_t t105 = ((((uint8_t *)&t104))[0] != 0);
            F.ret = t103;
            goto gm_fret;
        }
        goto me23;
mn37:;
ma25:;
        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t107 != ((uint8_t)28ULL)) goto mn106;
        {
            void * t109 = gm_l0;
            gm_tl874t2atnlh3 t110 = gm_l1;
            uint64_t t111 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t111 >= (uintptr_t)(t112)) gm_trap_index(t111, t112, gm_srcn3jjc71a2bv3, 361, 41);
            gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t111 * 16));
            gm_b16_8 t114 = t113;
            gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
            gm_td0ept12e7q8b t116;
            gm_f_sys_src_5svsr6i((void *)&t116, (void *)NULL, cx, NULL, t109, t110, t115);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t117 = t116;
            if (((((uint8_t *)&t117))[0] != 0)) {
                gm_t76123gfet73g t118 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t117) + 8));
                gm_tarq9lp8tdk6f t119;
                GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
                ((uint8_t *)&t119)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t119 + 8), t118);
                F.ret = t119;
                goto gm_fret;
            }
            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
            F.l12 = t120;
        }
        {
            void * t121 = gm_l0;
            uint64_t t122 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t122 >= (uintptr_t)(t123)) gm_trap_index(t122, t123, gm_srcn3jjc71a2bv3, 362, 32);
            gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t122 * 16));
            gm_b16_8 t125 = t124;
            gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
            unsigned __int128 t127 = gm_f_get_uint_bq8c4b7(t121, t126);
            uint16_t t128 = (uint16_t)t127;
            F.l13 = t128;
        }
        {
            gm_toe5fmr3vvf4d t129;
            GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)12ULL));
            gm_t62ebbi2i5rkd t130;
            GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
            ((uint8_t *)&t130)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t130 + 8), t129);
            F.l14 = t130;
        }
        {
            gm_closure t131 = ((gm_closure){(void *)gm_f_fn_cijad0r, (void *)&F.h, cx});
            gm_f_defaults_p077jgf(t131);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t62ebbi2i5rkd t132 = F.l14;
            gm_t62ebbi2i5rkd t133 = t132;
            gm_tm98ap4112hee t134;
            if (!((((uint8_t *)&t133))[0] != 0)) {
                gm_tm98ap4112hee t135 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t133) + 8));
                t134 = t135;
            } else {
                gm_toe5fmr3vvf4d t136 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t133) + 8));
                gm_l15 = t136;
                gm_toe5fmr3vvf4d t137 = gm_l15;
                int64_t t138 = gm_f_sys_code_cjtlfo1(t137);
                gm_tarq9lp8tdk6f t139;
                GM_ST(uint8_t, (uint8_t *)&t139 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t139 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t139 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t139 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t139 + 24, 0);
                ((uint8_t *)&t139)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t139 + 8), t138);
                gm_tarq9lp8tdk6f t140 = t139;
                uint8_t t141 = ((((uint8_t *)&t140))[0] != 0);
                F.ret = t139;
                goto gm_fret;
            }
            gm_l16 = t134;
        }
        {
            void * t142 = gm_l0;
            void * t143 = (void *)(((uint8_t *)t142) + 1352);
            int64_t t144 = gm_f_sys_next_handle_r35ek81(t143);
            gm_l17 = t144;
        }
        {
            void * t145 = gm_l0;
            void * t146 = (void *)((((uint8_t *)t145) + 1352) + 168);
            int64_t t147 = gm_l17;
            gm_tm98ap4112hee t148 = gm_l16;
            gm_tftb1odc739t5 t149 = gm_f_put_ha6815g(t146, t147, GM_CONV(gm_csfq64m1m1rt6m, t148));
            gm_tftb1odc739t5 t150 = t149;
            if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 1, gm_srcn3jjc71a2bv3, 367, 13);
            gm_b0_1 t151 = GM_LD(gm_b0_1, (((uint8_t *)&t150) + 1));
        }
        {
            int64_t t152 = gm_l17;
            gm_tarq9lp8tdk6f t153;
            GM_ST(uint8_t, (uint8_t *)&t153 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t153 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t153 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t153 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t153 + 24, 0);
            ((uint8_t *)&t153)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t153 + 8), t152);
            gm_tarq9lp8tdk6f t154 = t153;
            uint8_t t155 = ((((uint8_t *)&t154))[0] != 0);
            F.ret = t153;
            goto gm_fret;
        }
        goto me23;
mn106:;
ma26:;
        uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t157 != ((uint8_t)29ULL)) goto mn156;
        {
            void * t159 = gm_l0;
            void * t160 = (void *)((((uint8_t *)t159) + 1352) + 168);
            int64_t t161 = gm_l5;
            gm_tppo2hmut88dq t162 = gm_f_get_8o0rtd2(t160, t161);
            gm_tppo2hmut88dq t163 = t162;
            gm_tm98ap4112hee t164;
            if (((((uint8_t *)&t163))[8] != 0)) {
                gm_tm98ap4112hee t165 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t163) + 0));
                t164 = t165;
            } else {
                int64_t t166 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t167;
                GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t167 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t167 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t167 + 24, 0);
                ((uint8_t *)&t167)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t167 + 8), t166);
                gm_tarq9lp8tdk6f t168 = t167;
                uint8_t t169 = ((((uint8_t *)&t168))[0] != 0);
                F.ret = t167;
                goto gm_fret;
            }
            F.l18 = t164;
        }
        {
            gm_toe5fmr3vvf4d t170;
            GM_ST(uint8_t, (uint8_t *)&t170 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t170 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t170 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)12ULL));
            gm_tvcqs9u9iqpct t171;
            GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
            ((uint8_t *)&t171)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t171 + 4), t170);
            F.l19 = t171;
        }
        {
            gm_closure t172 = ((gm_closure){(void *)gm_f_fn_mslt7ot, (void *)&F.h, cx});
            gm_f_defaults_p077jgf(t172);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tvcqs9u9iqpct t173 = F.l19;
            gm_tvcqs9u9iqpct t174 = t173;
            uint16_t t175;
            if (!((((uint8_t *)&t174))[0] != 0)) {
                uint16_t t176 = GM_LD(uint16_t, (((uint8_t *)&t174) + 4));
                t175 = t176;
            } else {
                gm_toe5fmr3vvf4d t177 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t174) + 4));
                gm_l20 = t177;
                gm_toe5fmr3vvf4d t178 = gm_l20;
                int64_t t179 = gm_f_sys_code_cjtlfo1(t178);
                gm_tarq9lp8tdk6f t180;
                GM_ST(uint8_t, (uint8_t *)&t180 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t180 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t180 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t180 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t180 + 24, 0);
                ((uint8_t *)&t180)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t180 + 8), t179);
                gm_tarq9lp8tdk6f t181 = t180;
                uint8_t t182 = ((((uint8_t *)&t181))[0] != 0);
                F.ret = t180;
                goto gm_fret;
            }
            gm_l21 = t175;
        }
        {
            uint16_t t183 = gm_l21;
            int64_t t184 = (int64_t)t183;
            gm_tarq9lp8tdk6f t185;
            GM_ST(uint8_t, (uint8_t *)&t185 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t185 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t185 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t185 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t185 + 24, 0);
            ((uint8_t *)&t185)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t185 + 8), t184);
            gm_tarq9lp8tdk6f t186 = t185;
            uint8_t t187 = ((((uint8_t *)&t186))[0] != 0);
            F.ret = t185;
            goto gm_fret;
        }
        goto me23;
mn156:;
ma27:;
        uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t189 != ((uint8_t)30ULL)) goto mn188;
        {
            void * t191 = gm_l0;
            void * t192 = (void *)((((uint8_t *)t191) + 1352) + 168);
            int64_t t193 = gm_l5;
            gm_tppo2hmut88dq t194 = gm_f_get_8o0rtd2(t192, t193);
            gm_tppo2hmut88dq t195 = t194;
            gm_tm98ap4112hee t196;
            if (((((uint8_t *)&t195))[8] != 0)) {
                gm_tm98ap4112hee t197 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t195) + 0));
                t196 = t197;
            } else {
                int64_t t198 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t199;
                GM_ST(uint8_t, (uint8_t *)&t199 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t199 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t199 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t199 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t199 + 24, 0);
                ((uint8_t *)&t199)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t199 + 8), t198);
                gm_tarq9lp8tdk6f t200 = t199;
                uint8_t t201 = ((((uint8_t *)&t200))[0] != 0);
                F.ret = t199;
                goto gm_fret;
            }
            F.l22 = t196;
        }
        {
            gm_toe5fmr3vvf4d t202;
            GM_ST(uint8_t, (uint8_t *)&t202 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t202 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t202 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t202 + 0), ((uint8_t)12ULL));
            gm_t62ebbi2i5rkd t203;
            GM_ST(uint8_t, (uint8_t *)&t203 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t203 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t203 + 4, 0);
            ((uint8_t *)&t203)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t203 + 8), t202);
            F.l23 = t203;
        }
        {
            void * t205 = gm_l0;
            uint64_t t206 = GM_LD(uintptr_t, (((((uint8_t *)t205) + 1656) + 0) + 0) + 8);
            uint8_t t207 = (t206 > ((uint64_t)1ULL));
            if (!t207) goto ie204;
            {
                void * t209 = gm_l0;
                gm_tl874t2atnlh3 t210 = gm_l1;
                gm_tm98ap4112hee t211 = F.l22;
                gm_tmvdbo28o1i6h t212;
                GM_ST(uint8_t, (uint8_t *)&t212 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t212 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t212 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t212 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t212 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t212 + 0), ((uint8_t)3ULL));
                GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t212 + 8), t211);
                gm_toqti1ubjnfd1 t213;
                gm_f_socket_on_host_thread_u8c2vg5((void *)&t213, (void *)NULL, cx, NULL, t209, t210, &t212);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_toqti1ubjnfd1 t214 = t213;
                if (((((uint8_t *)&t214))[0] != 0)) {
                    gm_t76123gfet73g t215 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t214) + 8));
                    gm_tarq9lp8tdk6f t216;
                    GM_ST(uint8_t, (uint8_t *)&t216 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t216 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t216 + 4, 0);
                    ((uint8_t *)&t216)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t216 + 8), t215);
                    F.ret = t216;
                    goto gm_fret;
                }
                gm_t62ebbi2i5rkd t217 = GM_LD(gm_t62ebbi2i5rkd, (((uint8_t *)&t214) + 8));
                F.l23 = t217;
            }
            goto ix204;
ie204:;
            {
                gm_closure t219 = ((gm_closure){(void *)gm_f_fn_rg62rr4, (void *)&F.h, cx});
                gm_f_defaults_p077jgf(t219);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix204:;
        }
        {
            gm_t62ebbi2i5rkd t220 = F.l23;
            gm_t62ebbi2i5rkd t221 = t220;
            gm_tm98ap4112hee t222;
            if (!((((uint8_t *)&t221))[0] != 0)) {
                gm_tm98ap4112hee t223 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t221) + 8));
                t222 = t223;
            } else {
                gm_toe5fmr3vvf4d t224 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t221) + 8));
                gm_l24 = t224;
                gm_toe5fmr3vvf4d t225 = gm_l24;
                int64_t t226 = gm_f_sys_code_cjtlfo1(t225);
                gm_tarq9lp8tdk6f t227;
                GM_ST(uint8_t, (uint8_t *)&t227 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t227 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t227 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t227 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t227 + 24, 0);
                ((uint8_t *)&t227)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t227 + 8), t226);
                gm_tarq9lp8tdk6f t228 = t227;
                uint8_t t229 = ((((uint8_t *)&t228))[0] != 0);
                F.ret = t227;
                goto gm_fret;
            }
            gm_l25 = t222;
        }
        {
            void * t230 = gm_l0;
            void * t231 = (void *)(((uint8_t *)t230) + 1352);
            int64_t t232 = gm_f_sys_next_handle_r35ek81(t231);
            gm_l26 = t232;
        }
        {
            void * t233 = gm_l0;
            void * t234 = (void *)((((uint8_t *)t233) + 1352) + 112);
            int64_t t235 = gm_l26;
            gm_tm98ap4112hee t236 = gm_l25;
            gm_tftb1odc739t5 t237 = gm_f_put_qkbvor7(t234, t235, GM_CONV(gm_csfq64m1m1rt6m, t236));
            gm_tftb1odc739t5 t238 = t237;
            if (((((uint8_t *)&t238))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t238) + 1, gm_srcn3jjc71a2bv3, 387, 13);
            gm_b0_1 t239 = GM_LD(gm_b0_1, (((uint8_t *)&t238) + 1));
        }
        {
            int64_t t240 = gm_l26;
            gm_tarq9lp8tdk6f t241;
            GM_ST(uint8_t, (uint8_t *)&t241 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t241 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t241 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t241 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t241 + 24, 0);
            ((uint8_t *)&t241)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t241 + 8), t240);
            gm_tarq9lp8tdk6f t242 = t241;
            uint8_t t243 = ((((uint8_t *)&t242))[0] != 0);
            F.ret = t241;
            goto gm_fret;
        }
        goto me23;
mn188:;
ma28:;
        uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t245 != ((uint8_t)31ULL)) goto mn244;
        {
            void * t247 = gm_l0;
            gm_tl874t2atnlh3 t248 = gm_l1;
            uint64_t t249 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t250 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t249 >= (uintptr_t)(t250)) gm_trap_index(t249, t250, gm_srcn3jjc71a2bv3, 391, 41);
            gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t249 * 16));
            gm_b16_8 t252 = t251;
            gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)&t252));
            gm_td0ept12e7q8b t254;
            gm_f_sys_src_5svsr6i((void *)&t254, (void *)NULL, cx, NULL, t247, t248, t253);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t255 = t254;
            if (((((uint8_t *)&t255))[0] != 0)) {
                gm_t76123gfet73g t256 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t255) + 8));
                gm_tarq9lp8tdk6f t257;
                GM_ST(uint8_t, (uint8_t *)&t257 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t257 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t257 + 4, 0);
                ((uint8_t *)&t257)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t257 + 8), t256);
                F.ret = t257;
                goto gm_fret;
            }
            gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&t255) + 8));
            F.l27 = t258;
        }
        {
            void * t259 = gm_l0;
            void * t260 = (void *)((((uint8_t *)t259) + 1352) + 112);
            int64_t t261 = gm_l5;
            gm_tppo2hmut88dq t262 = gm_f_get_gej4j8d(t260, t261);
            gm_tppo2hmut88dq t263 = t262;
            gm_tm98ap4112hee t264;
            if (((((uint8_t *)&t263))[8] != 0)) {
                gm_tm98ap4112hee t265 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t263) + 0));
                t264 = t265;
            } else {
                int64_t t266 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t267;
                GM_ST(uint8_t, (uint8_t *)&t267 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t267 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t267 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t267 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t267 + 24, 0);
                ((uint8_t *)&t267)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t267 + 8), t266);
                gm_tarq9lp8tdk6f t268 = t267;
                uint8_t t269 = ((((uint8_t *)&t268))[0] != 0);
                F.ret = t267;
                goto gm_fret;
            }
            F.l28 = t264;
        }
        {
            gm_toe5fmr3vvf4d t270;
            GM_ST(uint8_t, (uint8_t *)&t270 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t270 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t270 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t270 + 0), ((uint8_t)12ULL));
            gm_thf5kr0is9u9i t271;
            GM_ST(uint8_t, (uint8_t *)&t271 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t271 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t271 + 4, 0);
            ((uint8_t *)&t271)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t271 + 8), t270);
            F.l29 = t271;
        }
        {
            void * t273 = gm_l0;
            uint64_t t274 = GM_LD(uintptr_t, (((((uint8_t *)t273) + 1656) + 0) + 0) + 8);
            uint8_t t275 = (t274 > ((uint64_t)1ULL));
            if (!t275) goto ie272;
            {
                void * t277 = gm_l0;
                gm_tl874t2atnlh3 t278 = gm_l1;
                gm_t3olsle24rveu t279;
                gm_tm98ap4112hee t280 = F.l28;
                GM_ST(gm_tm98ap4112hee, (uint8_t *)&t279 + 0, t280);
                gm_b16_8 t281 = F.l27;
                GM_ST(gm_b16_8, (uint8_t *)&t279 + 8, t281);
                gm_tmvdbo28o1i6h t282;
                GM_ST(uint8_t, (uint8_t *)&t282 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t282 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t282 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t282 + 0), ((uint8_t)4ULL));
                GM_ST(gm_t3olsle24rveu, ((uint8_t *)&t282 + 8), t279);
                gm_t6j8vdmi81h7c t283;
                gm_f_on_host_thread_kmegc07((void *)&t283, (void *)NULL, cx, NULL, t277, t278, &t282);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6j8vdmi81h7c t284 = t283;
                if (((((uint8_t *)&t284))[0] != 0)) {
                    gm_t76123gfet73g t285 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t284) + 8));
                    gm_tarq9lp8tdk6f t286;
                    GM_ST(uint8_t, (uint8_t *)&t286 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t286 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t286 + 4, 0);
                    ((uint8_t *)&t286)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t286 + 8), t285);
                    F.ret = t286;
                    goto gm_fret;
                }
                gm_tfvg1o4f8mmhi t287 = GM_LD(gm_tfvg1o4f8mmhi, (((uint8_t *)&t284) + 8));
                gm_tfvg1o4f8mmhi t288 = t287;
                gm_thf5kr0is9u9i t289 = GM_LD(gm_thf5kr0is9u9i, (((uint8_t *)&t288) + 0));
                F.l29 = t289;
            }
            goto ix272;
ie272:;
            {
                gm_closure t291 = ((gm_closure){(void *)gm_f_fn_tlii61s, (void *)&F.h, cx});
                gm_f_defaults_p077jgf(t291);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix272:;
        }
        {
            gm_thf5kr0is9u9i t292 = F.l29;
            int64_t t293 = gm_f_sys_net_count_46l7ueq(t292);
            gm_tarq9lp8tdk6f t294;
            GM_ST(uint8_t, (uint8_t *)&t294 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t294 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t294 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t294 + 24, 0);
            ((uint8_t *)&t294)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t294 + 8), t293);
            gm_tarq9lp8tdk6f t295 = t294;
            uint8_t t296 = ((((uint8_t *)&t295))[0] != 0);
            F.ret = t294;
            goto gm_fret;
        }
        goto me23;
mn244:;
ma29:;
        uint8_t t298 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t298 != ((uint8_t)32ULL)) goto mn297;
        {
            void * t300 = gm_l0;
            gm_tl874t2atnlh3 t301 = gm_l1;
            uint64_t t302 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t303 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t302 >= (uintptr_t)(t303)) gm_trap_index(t302, t303, gm_srcn3jjc71a2bv3, 402, 41);
            gm_b16_8 t304 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t302 * 16));
            gm_b16_8 t305 = t304;
            gm_b16_8 t306 = GM_LD(gm_b16_8, ((uint8_t *)&t305));
            gm_td0ept12e7q8b t307;
            gm_f_sys_dest_rtrb14h((void *)&t307, (void *)NULL, cx, NULL, t300, t301, t306);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t308 = t307;
            if (((((uint8_t *)&t308))[0] != 0)) {
                gm_t76123gfet73g t309 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t308) + 8));
                gm_tarq9lp8tdk6f t310;
                GM_ST(uint8_t, (uint8_t *)&t310 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t310 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t310 + 4, 0);
                ((uint8_t *)&t310)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t310 + 8), t309);
                F.ret = t310;
                goto gm_fret;
            }
            gm_b16_8 t311 = GM_LD(gm_b16_8, (((uint8_t *)&t308) + 8));
            F.l30 = t311;
        }
        {
            void * t312 = gm_l0;
            void * t313 = (void *)((((uint8_t *)t312) + 1352) + 112);
            int64_t t314 = gm_l5;
            gm_tppo2hmut88dq t315 = gm_f_get_gej4j8d(t313, t314);
            gm_tppo2hmut88dq t316 = t315;
            gm_tm98ap4112hee t317;
            if (((((uint8_t *)&t316))[8] != 0)) {
                gm_tm98ap4112hee t318 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t316) + 0));
                t317 = t318;
            } else {
                int64_t t319 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t320;
                GM_ST(uint8_t, (uint8_t *)&t320 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t320 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t320 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t320 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t320 + 24, 0);
                ((uint8_t *)&t320)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t320 + 8), t319);
                gm_tarq9lp8tdk6f t321 = t320;
                uint8_t t322 = ((((uint8_t *)&t321))[0] != 0);
                F.ret = t320;
                goto gm_fret;
            }
            F.l31 = t317;
        }
        {
            gm_toe5fmr3vvf4d t323;
            GM_ST(uint8_t, (uint8_t *)&t323 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t323 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t323 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t323 + 0), ((uint8_t)12ULL));
            gm_thf5kr0is9u9i t324;
            GM_ST(uint8_t, (uint8_t *)&t324 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t324 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t324 + 4, 0);
            ((uint8_t *)&t324)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t324 + 8), t323);
            F.l32 = t324;
        }
        {
            void * t326 = gm_l0;
            uint64_t t327 = GM_LD(uintptr_t, (((((uint8_t *)t326) + 1656) + 0) + 0) + 8);
            uint8_t t328 = (t327 > ((uint64_t)1ULL));
            if (!t328) goto ie325;
            {
                void * t330 = gm_l0;
                gm_tl874t2atnlh3 t331 = gm_l1;
                gm_t3olsle24rveu t332;
                gm_tm98ap4112hee t333 = F.l31;
                GM_ST(gm_tm98ap4112hee, (uint8_t *)&t332 + 0, t333);
                gm_b16_8 t334 = F.l30;
                GM_ST(gm_b16_8, (uint8_t *)&t332 + 8, t334);
                gm_tmvdbo28o1i6h t335;
                GM_ST(uint8_t, (uint8_t *)&t335 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t335 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t335 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t335 + 0), ((uint8_t)5ULL));
                GM_ST(gm_t3olsle24rveu, ((uint8_t *)&t335 + 8), t332);
                gm_t6j8vdmi81h7c t336;
                gm_f_on_host_thread_kmegc07((void *)&t336, (void *)NULL, cx, NULL, t330, t331, &t335);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6j8vdmi81h7c t337 = t336;
                if (((((uint8_t *)&t337))[0] != 0)) {
                    gm_t76123gfet73g t338 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t337) + 8));
                    gm_tarq9lp8tdk6f t339;
                    GM_ST(uint8_t, (uint8_t *)&t339 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t339 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t339 + 4, 0);
                    ((uint8_t *)&t339)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t339 + 8), t338);
                    F.ret = t339;
                    goto gm_fret;
                }
                gm_tfvg1o4f8mmhi t340 = GM_LD(gm_tfvg1o4f8mmhi, (((uint8_t *)&t337) + 8));
                gm_tfvg1o4f8mmhi t341 = t340;
                gm_thf5kr0is9u9i t342 = GM_LD(gm_thf5kr0is9u9i, (((uint8_t *)&t341) + 0));
                F.l32 = t342;
            }
            goto ix325;
ie325:;
            {
                gm_closure t344 = ((gm_closure){(void *)gm_f_fn_ducmvdu, (void *)&F.h, cx});
                gm_f_defaults_p077jgf(t344);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix325:;
        }
        {
            gm_thf5kr0is9u9i t345 = F.l32;
            int64_t t346 = gm_f_sys_net_count_46l7ueq(t345);
            gm_tarq9lp8tdk6f t347;
            GM_ST(uint8_t, (uint8_t *)&t347 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t347 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t347 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t347 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t347 + 24, 0);
            ((uint8_t *)&t347)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t347 + 8), t346);
            gm_tarq9lp8tdk6f t348 = t347;
            uint8_t t349 = ((((uint8_t *)&t348))[0] != 0);
            F.ret = t347;
            goto gm_fret;
        }
        goto me23;
mn297:;
ma30:;
        uint8_t t351 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t351 != ((uint8_t)33ULL)) goto mn350;
        {
            void * t354 = gm_l0;
            void * t355 = (void *)((((uint8_t *)t354) + 1352) + 112);
            int64_t t356 = gm_l5;
            gm_tppo2hmut88dq t357 = gm_f_remove_7bvuc21(t355, t356);
            gm_tppo2hmut88dq t358 = t357;
            if (!((((uint8_t *)&t358))[8] != 0)) goto ie353;
            gm_tm98ap4112hee t359 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&t358));
            F.l33 = t359;
            {
                gm_closure t361 = ((gm_closure){(void *)gm_f_fn_r8m0qjr, (void *)&F.h, cx});
                gm_f_defaults_p077jgf(t361);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tarq9lp8tdk6f t362;
                GM_ST(uint8_t, (uint8_t *)&t362 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t362 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t362 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t362 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t362 + 24, 0);
                ((uint8_t *)&t362)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t362 + 8), ((int64_t)0LL));
                gm_tarq9lp8tdk6f t363 = t362;
                uint8_t t364 = ((((uint8_t *)&t363))[0] != 0);
                F.ret = t362;
                goto gm_fret;
            }
            goto ix353;
ie353:;
ix353:;
        }
        {
            void * t366 = gm_l0;
            void * t367 = (void *)((((uint8_t *)t366) + 1352) + 168);
            int64_t t368 = gm_l5;
            gm_tppo2hmut88dq t369 = gm_f_remove_3j2d0bb(t367, t368);
            gm_tppo2hmut88dq t370 = t369;
            if (!((((uint8_t *)&t370))[8] != 0)) goto ie365;
            gm_tm98ap4112hee t371 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&t370));
            F.l34 = t371;
            {
                gm_closure t373 = ((gm_closure){(void *)gm_f_fn_1lff729, (void *)&F.h, cx});
                gm_f_defaults_p077jgf(t373);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tarq9lp8tdk6f t374;
                GM_ST(uint8_t, (uint8_t *)&t374 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t374 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t374 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t374 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t374 + 24, 0);
                ((uint8_t *)&t374)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t374 + 8), ((int64_t)0LL));
                gm_tarq9lp8tdk6f t375 = t374;
                uint8_t t376 = ((((uint8_t *)&t375))[0] != 0);
                F.ret = t374;
                goto gm_fret;
            }
            goto ix365;
ie365:;
ix365:;
        }
        {
            int64_t t377 = GM_LD(int64_t, gm_c3qosqjl81k);
            gm_tarq9lp8tdk6f t378;
            GM_ST(uint8_t, (uint8_t *)&t378 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t378 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t378 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t378 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t378 + 24, 0);
            ((uint8_t *)&t378)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t378 + 8), t377);
            gm_tarq9lp8tdk6f t379 = t378;
            uint8_t t380 = ((((uint8_t *)&t379))[0] != 0);
            F.ret = t378;
            goto gm_fret;
        }
        goto me23;
mn350:;
ma31:;
        uint8_t t382 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t382 != ((uint8_t)34ULL)) goto mn381;
        {
            void * t384 = gm_l0;
            uint64_t t385 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t386 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t385 >= (uintptr_t)(t386)) gm_trap_index(t385, t386, gm_srcn3jjc71a2bv3, 424, 50);
            void * t387 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t385 * 8));
            gm_t88rcucg1b7uk t388 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t387) + 96));
            uint64_t t389 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t390 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t389 >= (uintptr_t)(t390)) gm_trap_index(t389, t390, gm_srcn3jjc71a2bv3, 424, 62);
            gm_b16_8 t391 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t389 * 16));
            gm_b16_8 t392 = t391;
            gm_b16_8 t393 = GM_LD(gm_b16_8, ((uint8_t *)&t392));
            int64_t t394 = gm_f_sys_int_32e79ed(t384, GM_CONV(gm_csguankko4lmhd, t388), t393);
            int64_t t395 = t394;
            gm_t9qkh0rsooc8d t396;
            int64_t t399 = GM_LD(int64_t, ((uint8_t *)&t395));
            if (!(t399 == ((int64_t)0LL))) goto mn398;
            gm_t9qkh0rsooc8d t400;
            GM_ST(uint8_t, ((uint8_t *)&t400 + 0), ((uint8_t)0ULL));
            t396 = t400;
            goto me397;
mn398:;
            int64_t t402 = GM_LD(int64_t, ((uint8_t *)&t395));
            if (!(t402 == ((int64_t)1LL))) goto mn401;
            gm_t9qkh0rsooc8d t403;
            GM_ST(uint8_t, ((uint8_t *)&t403 + 0), ((uint8_t)1ULL));
            t396 = t403;
            goto me397;
mn401:;
            gm_t9qkh0rsooc8d t405;
            GM_ST(uint8_t, ((uint8_t *)&t405 + 0), ((uint8_t)2ULL));
            t396 = t405;
            goto me397;
mn404:;
            gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 424, 33);
me397:;
            F.l35 = t396;
        }
        {
            void * t406 = gm_l0;
            void * t407 = (void *)((((uint8_t *)t406) + 1352) + 112);
            int64_t t408 = gm_l5;
            gm_tppo2hmut88dq t409 = gm_f_get_gej4j8d(t407, t408);
            gm_tppo2hmut88dq t410 = t409;
            gm_tm98ap4112hee t411;
            if (((((uint8_t *)&t410))[8] != 0)) {
                gm_tm98ap4112hee t412 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t410) + 0));
                t411 = t412;
            } else {
                int64_t t413 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t414;
                GM_ST(uint8_t, (uint8_t *)&t414 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t414 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t414 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t414 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t414 + 24, 0);
                ((uint8_t *)&t414)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t414 + 8), t413);
                gm_tarq9lp8tdk6f t415 = t414;
                uint8_t t416 = ((((uint8_t *)&t415))[0] != 0);
                F.ret = t414;
                goto gm_fret;
            }
            F.l36 = t411;
        }
        {
            gm_toe5fmr3vvf4d t417;
            GM_ST(uint8_t, (uint8_t *)&t417 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t417 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t417 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t417 + 0), ((uint8_t)12ULL));
            gm_ttln0d0hd70en t418;
            GM_ST(uint8_t, (uint8_t *)&t418 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t418 + 2, 0);
            ((uint8_t *)&t418)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t418 + 4), t417);
            F.l37 = t418;
        }
        {
            gm_closure t419 = ((gm_closure){(void *)gm_f_fn_vhnrvmh, (void *)&F.h, cx});
            gm_f_defaults_p077jgf(t419);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ttln0d0hd70en t420 = F.l37;
            int64_t t421 = gm_f_sys_unit_nrsqciq(t420);
            gm_tarq9lp8tdk6f t422;
            GM_ST(uint8_t, (uint8_t *)&t422 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t422 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t422 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t422 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t422 + 24, 0);
            ((uint8_t *)&t422)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t422 + 8), t421);
            gm_tarq9lp8tdk6f t423 = t422;
            uint8_t t424 = ((((uint8_t *)&t423))[0] != 0);
            F.ret = t422;
            goto gm_fret;
        }
        goto me23;
mn381:;
ma32:;
        uint8_t t426 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t426 != ((uint8_t)35ULL)) goto mn425;
        {
            void * t428 = gm_l0;
            uint64_t t429 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t430 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t429 >= (uintptr_t)(t430)) gm_trap_index(t429, t430, gm_srcn3jjc71a2bv3, 435, 35);
            gm_b16_8 t431 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t429 * 16));
            gm_b16_8 t432 = t431;
            gm_b16_8 t433 = GM_LD(gm_b16_8, ((uint8_t *)&t432));
            unsigned __int128 t434 = gm_f_get_uint_bq8c4b7(t428, t433);
            uint64_t t435 = (uint64_t)t434;
            gm_l38 = t435;
        }
        {
            void * t436 = gm_l0;
            uint64_t t437 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t438 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t437 >= (uintptr_t)(t438)) gm_trap_index(t437, t438, gm_srcn3jjc71a2bv3, 436, 35);
            gm_b16_8 t439 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t437 * 16));
            gm_b16_8 t440 = t439;
            gm_b16_8 t441 = GM_LD(gm_b16_8, ((uint8_t *)&t440));
            unsigned __int128 t442 = gm_f_get_uint_bq8c4b7(t436, t441);
            uint64_t t443 = (uint64_t)t442;
            gm_l39 = t443;
        }
        {
            void * t444 = gm_l0;
            void * t445 = (void *)((((uint8_t *)t444) + 1352) + 112);
            int64_t t446 = gm_l5;
            gm_tppo2hmut88dq t447 = gm_f_get_gej4j8d(t445, t446);
            gm_tppo2hmut88dq t448 = t447;
            gm_tm98ap4112hee t449;
            if (((((uint8_t *)&t448))[8] != 0)) {
                gm_tm98ap4112hee t450 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t448) + 0));
                t449 = t450;
            } else {
                int64_t t451 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t452;
                GM_ST(uint8_t, (uint8_t *)&t452 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t452 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t452 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t452 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t452 + 24, 0);
                ((uint8_t *)&t452)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t452 + 8), t451);
                gm_tarq9lp8tdk6f t453 = t452;
                uint8_t t454 = ((((uint8_t *)&t453))[0] != 0);
                F.ret = t452;
                goto gm_fret;
            }
            F.l40 = t449;
        }
        {
            gm_t6ie8iajlnpt1 t455;
            uint64_t t457 = gm_l38;
            uint8_t t458 = (t457 > ((uint64_t)0ULL));
            if (!t458) goto ie456;
            uint64_t t460 = gm_l38;
            gm_t4ce0jd3tdro4 t461;
            GM_ST(uint64_t, ((uint8_t *)&t461 + 0), t460);
            gm_t4ce0jd3tdro4 t462 = t461;
            gm_t6ie8iajlnpt1 t463;
            GM_ST(uint8_t, (uint8_t *)&t463 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t463 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t463 + 12, 0);
            GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t463), t462);
            ((uint8_t *)&t463)[8] = 1;
            t455 = t463;
            goto ix456;
ie456:;
            gm_t6ie8iajlnpt1 t465;
            memset(&t465, 0, sizeof t465);
            gm_t6ie8iajlnpt1 t466 = t465;
            t455 = t466;
ix456:;
            F.l41 = t455;
        }
        {
            gm_t6ie8iajlnpt1 t467;
            uint64_t t469 = gm_l39;
            uint8_t t470 = (t469 > ((uint64_t)0ULL));
            if (!t470) goto ie468;
            uint64_t t472 = gm_l39;
            gm_t4ce0jd3tdro4 t473;
            GM_ST(uint64_t, ((uint8_t *)&t473 + 0), t472);
            gm_t4ce0jd3tdro4 t474 = t473;
            gm_t6ie8iajlnpt1 t475;
            GM_ST(uint8_t, (uint8_t *)&t475 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t475 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t475 + 12, 0);
            GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t475), t474);
            ((uint8_t *)&t475)[8] = 1;
            t467 = t475;
            goto ix468;
ie468:;
            gm_t6ie8iajlnpt1 t477;
            memset(&t477, 0, sizeof t477);
            gm_t6ie8iajlnpt1 t478 = t477;
            t467 = t478;
ix468:;
            F.l42 = t467;
        }
        {
            gm_toe5fmr3vvf4d t479;
            GM_ST(uint8_t, (uint8_t *)&t479 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t479 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t479 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t479 + 0), ((uint8_t)12ULL));
            gm_ttln0d0hd70en t480;
            GM_ST(uint8_t, (uint8_t *)&t480 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t480 + 2, 0);
            ((uint8_t *)&t480)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t480 + 4), t479);
            F.l43 = t480;
        }
        {
            gm_closure t481 = ((gm_closure){(void *)gm_f_fn_j9i1sr8, (void *)&F.h, cx});
            gm_f_defaults_p077jgf(t481);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ttln0d0hd70en t482 = F.l43;
            int64_t t483 = gm_f_sys_unit_nrsqciq(t482);
            gm_tarq9lp8tdk6f t484;
            GM_ST(uint8_t, (uint8_t *)&t484 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t484 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t484 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t484 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t484 + 24, 0);
            ((uint8_t *)&t484)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t484 + 8), t483);
            gm_tarq9lp8tdk6f t485 = t484;
            uint8_t t486 = ((((uint8_t *)&t485))[0] != 0);
            F.ret = t484;
            goto gm_fret;
        }
        goto me23;
mn425:;
ma33:;
        uint8_t t488 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t488 != ((uint8_t)36ULL)) goto mn487;
        {
            void * t490 = gm_l0;
            void * t491 = (void *)((((uint8_t *)t490) + 1352) + 112);
            int64_t t492 = gm_l5;
            gm_tppo2hmut88dq t493 = gm_f_get_gej4j8d(t491, t492);
            gm_tppo2hmut88dq t494 = t493;
            gm_tm98ap4112hee t495;
            if (((((uint8_t *)&t494))[8] != 0)) {
                gm_tm98ap4112hee t496 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t494) + 0));
                t495 = t496;
            } else {
                int64_t t497 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t498;
                GM_ST(uint8_t, (uint8_t *)&t498 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t498 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t498 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t498 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t498 + 24, 0);
                ((uint8_t *)&t498)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t498 + 8), t497);
                gm_tarq9lp8tdk6f t499 = t498;
                uint8_t t500 = ((((uint8_t *)&t499))[0] != 0);
                F.ret = t498;
                goto gm_fret;
            }
            F.l44 = t495;
        }
        {
            void * t501 = gm_l0;
            gm_tl874t2atnlh3 t502 = gm_l1;
            uint64_t t503 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t504 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t503 >= (uintptr_t)(t504)) gm_trap_index(t503, t504, gm_srcn3jjc71a2bv3, 446, 41);
            gm_b16_8 t505 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t503 * 16));
            gm_b16_8 t506 = t505;
            gm_b16_8 t507 = GM_LD(gm_b16_8, ((uint8_t *)&t506));
            gm_td0ept12e7q8b t508;
            gm_f_sys_dest_rtrb14h((void *)&t508, (void *)NULL, cx, NULL, t501, t502, t507);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t509 = t508;
            if (((((uint8_t *)&t509))[0] != 0)) {
                gm_t76123gfet73g t510 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t509) + 8));
                gm_tarq9lp8tdk6f t511;
                GM_ST(uint8_t, (uint8_t *)&t511 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t511 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t511 + 4, 0);
                ((uint8_t *)&t511)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t511 + 8), t510);
                F.ret = t511;
                goto gm_fret;
            }
            gm_b16_8 t512 = GM_LD(gm_b16_8, (((uint8_t *)&t509) + 8));
            F.l45 = t512;
        }
        {
            gm_toe5fmr3vvf4d t513;
            GM_ST(uint8_t, (uint8_t *)&t513 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t513 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t513 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t513 + 0), ((uint8_t)12ULL));
            gm_thf5kr0is9u9i t514;
            GM_ST(uint8_t, (uint8_t *)&t514 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t514 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t514 + 4, 0);
            ((uint8_t *)&t514)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t514 + 8), t513);
            F.l46 = t514;
        }
        {
            gm_closure t515 = ((gm_closure){(void *)gm_f_fn_2gkkusq, (void *)&F.h, cx});
            gm_f_defaults_p077jgf(t515);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_thf5kr0is9u9i t516 = F.l46;
            int64_t t517 = gm_f_sys_count_50c5lcp(t516);
            gm_tarq9lp8tdk6f t518;
            GM_ST(uint8_t, (uint8_t *)&t518 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t518 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t518 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t518 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t518 + 24, 0);
            ((uint8_t *)&t518)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t518 + 8), t517);
            gm_tarq9lp8tdk6f t519 = t518;
            uint8_t t520 = ((((uint8_t *)&t519))[0] != 0);
            F.ret = t518;
            goto gm_fret;
        }
        goto me23;
mn487:;
ma34:;
        void * t522 = gm_l0;
        gm_tl874t2atnlh3 t523 = gm_l1;
        gm_b16_8 t524;
        GM_ST(void *, (uint8_t *)&t524, (void *)(gm_sd8o4m7695hpq));
        GM_ST(uintptr_t, (uint8_t *)&t524 + 8, (uintptr_t)(37));
        gm_t76123gfet73g t525;
        gm_f_trap_qh84l9d((void *)&t525, (void *)NULL, cx, NULL, t522, t523, t524);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tarq9lp8tdk6f t526;
        GM_ST(uint8_t, (uint8_t *)&t526 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t526 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t526 + 4, 0);
        ((uint8_t *)&t526)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t526 + 8), t525);
        gm_tarq9lp8tdk6f t527 = t526;
        uint8_t t528 = ((((uint8_t *)&t527))[0] != 0);
        F.ret = t526;
        goto gm_fret;
        goto me23;
mn521:;
ma35:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 345, 5);
me23:;
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
extern __attribute__((aligned(4))) int64_t gm_f_sys_net_code_ihgq920(gm_toe5fmr3vvf4d a0);
extern __attribute__((aligned(4))) int64_t gm_f_sys_net_count_46l7ueq(gm_thf5kr0is9u9i a0);
extern __attribute__((aligned(4))) int64_t gm_f_sys_next_handle_r35ek81(void * a0);
__attribute__((aligned(4))) void gm_f_sys_proc_jhdp277(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sys_proc_jhdp277 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l7; /* dir */
    gm_b16_8 gm_l8; /* program */
    uint32_t gm_l9; /* flags */
    gm_toe5fmr3vvf4d gm_l11; /* err */
    gm_tm98ap4112hee gm_l12; /* c */
    int64_t gm_l13; /* handle */
    int64_t gm_l14; /* h */
    gm_b16_8 gm_l15; /* data */
    gm_tm98ap4112hee gm_l16; /* c */
    gm_tm98ap4112hee gm_l17; /* c */
    gm_b16_8 gm_l18; /* buf */
    gm_tm98ap4112hee gm_l19; /* c */
    gm_toe5fmr3vvf4d gm_l20; /* err */
    gm_thdrvp9q0u7b5 gm_l21; /* got */
    gm_te0mgnjdphs16 gm_l22; /* chunk */
    gm_tm98ap4112hee gm_l23; /* c */
    gm_tlmjd3nndlf3r gm_l24; /* r */
    gm_toe5fmr3vvf4d gm_l25; /* err */
    gm_tsoiri44d0i6q gm_l26; /* status */
    uint32_t gm_l27; /* code */
    int32_t gm_l28; /* sig */
    gm_tm98ap4112hee gm_l29; /* c */
    gm_tr6norheal11n gm_l30; /* r */
    gm_toe5fmr3vvf4d gm_l31; /* err */
    gm_tqnlpelep1582 gm_l32; /* got */
    gm_tsoiri44d0i6q gm_l33; /* status */
    uint32_t gm_l34; /* code */
    int32_t gm_l35; /* sig */
    gm_tm98ap4112hee gm_l36; /* c */
    GM_STACK_CHECK(&F, 5423u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t3 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)37ULL)));
        if (!t3) goto ie2;
        {
            void * t5 = gm_l0;
            gm_tl874t2atnlh3 t6 = gm_l1;
            uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srcn3jjc71a2bv3, 470, 41);
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t7 * 16));
            gm_b16_8 t10 = t9;
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
            gm_t9922qjstblh1 t12;
            gm_f_sys_strings_r85ragc((void *)&t12, (void *)NULL, cx, NULL, t5, t6, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9922qjstblh1 t13 = t12;
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t76123gfet73g t14 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t13) + 8));
                gm_tarq9lp8tdk6f t15;
                GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t15 + 8), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_trqfvmjtt4sij t16 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t13) + 8));
            F.l5 = t16;
        }
        {
            void * t17 = gm_l0;
            gm_tl874t2atnlh3 t18 = gm_l1;
            uint64_t t19 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_srcn3jjc71a2bv3, 471, 40);
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t19 * 16));
            gm_b16_8 t22 = t21;
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
            gm_t9922qjstblh1 t24;
            gm_f_sys_strings_r85ragc((void *)&t24, (void *)NULL, cx, NULL, t17, t18, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9922qjstblh1 t25 = t24;
            if (((((uint8_t *)&t25))[0] != 0)) {
                gm_t76123gfet73g t26 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t25) + 8));
                gm_tarq9lp8tdk6f t27;
                GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
                ((uint8_t *)&t27)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t27 + 8), t26);
                F.ret = t27;
                goto gm_fret;
            }
            gm_trqfvmjtt4sij t28 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t25) + 8));
            F.l6 = t28;
        }
        {
            void * t29 = gm_l0;
            gm_tl874t2atnlh3 t30 = gm_l1;
            uint64_t t31 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_srcn3jjc71a2bv3, 472, 36);
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t31 * 16));
            gm_b16_8 t34 = t33;
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
            gm_td0ept12e7q8b t36;
            gm_f_sys_src_5svsr6i((void *)&t36, (void *)NULL, cx, NULL, t29, t30, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t37 = t36;
            if (((((uint8_t *)&t37))[0] != 0)) {
                gm_t76123gfet73g t38 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t37) + 8));
                gm_tarq9lp8tdk6f t39;
                GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                ((uint8_t *)&t39)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t39 + 8), t38);
                F.ret = t39;
                goto gm_fret;
            }
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
            gm_l7 = t40;
        }
        {
            void * t41 = gm_l0;
            gm_tl874t2atnlh3 t42 = gm_l1;
            uint64_t t43 = (uintptr_t)((uint64_t)3ULL);
            uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_srcn3jjc71a2bv3, 473, 40);
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t43 * 16));
            gm_b16_8 t46 = t45;
            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
            gm_td0ept12e7q8b t48;
            gm_f_sys_src_5svsr6i((void *)&t48, (void *)NULL, cx, NULL, t41, t42, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t49 = t48;
            if (((((uint8_t *)&t49))[0] != 0)) {
                gm_t76123gfet73g t50 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t49) + 8));
                gm_tarq9lp8tdk6f t51;
                GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                ((uint8_t *)&t51)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t51 + 8), t50);
                F.ret = t51;
                goto gm_fret;
            }
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 8));
            gm_l8 = t52;
        }
        {
            void * t53 = gm_l0;
            uint64_t t54 = (uintptr_t)((uint64_t)4ULL);
            uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t54 >= (uintptr_t)(t55)) gm_trap_index(t54, t55, gm_srcn3jjc71a2bv3, 474, 29);
            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t54 * 16));
            gm_b16_8 t57 = t56;
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
            unsigned __int128 t59 = gm_f_get_uint_bq8c4b7(t53, t58);
            uint32_t t60 = (uint32_t)t59;
            gm_l9 = t60;
        }
        {
        }
        {
        }
        {
            uint64_t t64 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            uint8_t t65 = ((t64 == ((uint64_t)0ULL)));
            if (!t65) goto ie63;
            {
                int64_t t67 = GM_LD(int64_t, gm_cndfnv7mbeh);
                gm_tarq9lp8tdk6f t68;
                GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t68 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t68 + 24, 0);
                ((uint8_t *)&t68)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t68 + 8), t67);
                gm_tarq9lp8tdk6f t69 = t68;
                uint8_t t70 = ((((uint8_t *)&t69))[0] != 0);
                F.ret = t68;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t70;
                goto gm_ld4_2;
            }
            goto ix63;
ie63:;
ix63:;
        }
        {
            memset((uint8_t *)&F.l10 + 3, 0, 5);
            memset((uint8_t *)&F.l10 + 50, 0, 6);
            uint32_t t71 = gm_l9;
            uint32_t t72 = (uint32_t)(t71 & ((uint32_t)3ULL));
            gm_t9qkh0rsooc8d t73 = gm_f_sys_stdio_nsl9o9u(t72);
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l10 + 0, t73);
            uint32_t t74 = gm_l9;
            unsigned __int128 n75 = (unsigned __int128)((uint64_t)2ULL);
            if (n75 >= 32) gm_trap_shift((uint64_t)n75, "u32", gm_srcn3jjc71a2bv3, 480, 32);
            uint32_t t76 = (uint32_t)(t74 >> (int)n75);
            uint32_t t77 = (uint32_t)(t76 & ((uint32_t)3ULL));
            gm_t9qkh0rsooc8d t78 = gm_f_sys_stdio_nsl9o9u(t77);
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l10 + 1, t78);
            uint32_t t79 = gm_l9;
            unsigned __int128 n80 = (unsigned __int128)((uint64_t)4ULL);
            if (n80 >= 32) gm_trap_shift((uint64_t)n80, "u32", gm_srcn3jjc71a2bv3, 481, 32);
            uint32_t t81 = (uint32_t)(t79 >> (int)n80);
            uint32_t t82 = (uint32_t)(t81 & ((uint32_t)3ULL));
            gm_t9qkh0rsooc8d t83 = gm_f_sys_stdio_nsl9o9u(t82);
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l10 + 2, t83);
            gm_th2lqbo77pvsc t84;
            uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            uint8_t t87 = (t86 > ((uint64_t)0ULL));
            if (!t87) goto ie85;
            gm_b16_8 t89 = gm_l7;
            gm_b16_8 t90 = t89;
            gm_th2lqbo77pvsc t91;
            GM_ST(uint8_t, (uint8_t *)&t91 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t91 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t91 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t91), t90);
            ((uint8_t *)&t91)[16] = 1;
            t84 = t91;
            goto ix85;
ie85:;
            gm_th2lqbo77pvsc t93;
            memset(&t93, 0, sizeof t93);
            gm_th2lqbo77pvsc t94 = t93;
            t84 = t94;
ix85:;
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l10 + 8, t84);
            gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t96 = t95;
            gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
            GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 32, t97);
            uint32_t t98 = gm_l9;
            uint8_t t99 = gm_f_sys_has_0sego5l(t98, ((uint32_t)64ULL));
            GM_ST(uint8_t, (uint8_t *)&F.l10 + 48, t99);
            uint32_t t100 = gm_l9;
            uint8_t t101 = gm_f_sys_has_0sego5l(t100, ((uint32_t)128ULL));
            GM_ST(uint8_t, (uint8_t *)&F.l10 + 49, t101);
            gm_th2lqbo77pvsc t102;
            uint32_t t104 = gm_l9;
            uint8_t t105 = gm_f_sys_has_0sego5l(t104, ((uint32_t)256ULL));
            if (!t105) goto ie103;
            gm_b16_8 t107 = gm_l8;
            gm_b16_8 t108 = t107;
            gm_th2lqbo77pvsc t109;
            GM_ST(uint8_t, (uint8_t *)&t109 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t109 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t109 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t109), t108);
            ((uint8_t *)&t109)[16] = 1;
            t102 = t109;
            goto ix103;
ie103:;
            gm_th2lqbo77pvsc t111;
            memset(&t111, 0, sizeof t111);
            gm_th2lqbo77pvsc t112 = t111;
            t102 = t112;
ix103:;
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l10 + 56, t102);
        }
        {
            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t114 = t113;
            gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
            gm_node *pn116 = gm_find(cx, 2119230);
            if (!pn116) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 488, 14);
            gm_t62ebbi2i5rkd t117;
            t117 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, const void *))pn116->clauses[0])(pn116->env, pn116->hctx, pn116, t115, &(*(gm_t32dckm4jfarl *)((uint8_t *)&F.l10)));
            if (gm_unw.kind) {
                goto gm_ld4_2;
            }
            gm_t62ebbi2i5rkd t118 = t117;
            gm_tm98ap4112hee t119;
            if (!((((uint8_t *)&t118))[0] != 0)) {
                gm_tm98ap4112hee t120 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t118) + 8));
                t119 = t120;
            } else {
                gm_toe5fmr3vvf4d t121 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t118) + 8));
                gm_l11 = t121;
                gm_toe5fmr3vvf4d t122 = gm_l11;
                int64_t t123 = gm_f_sys_code_cjtlfo1(t122);
                gm_tarq9lp8tdk6f t124;
                GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t124 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t124 + 24, 0);
                ((uint8_t *)&t124)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t124 + 8), t123);
                gm_tarq9lp8tdk6f t125 = t124;
                uint8_t t126 = ((((uint8_t *)&t125))[0] != 0);
                F.ret = t124;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t126;
                goto gm_ld4_2;
            }
            gm_l12 = t119;
        }
        {
            void * t127 = gm_l0;
            void * t128 = (void *)(((uint8_t *)t127) + 1352);
            int64_t t129 = gm_f_sys_next_handle_r35ek81(t128);
            gm_l13 = t129;
        }
        {
            void * t130 = gm_l0;
            void * t131 = (void *)((((uint8_t *)t130) + 1352) + 224);
            int64_t t132 = gm_l13;
            gm_tm98ap4112hee t133 = gm_l12;
            gm_tftb1odc739t5 t134 = gm_f_put_0tusk8g(t131, t132, GM_CONV(gm_csfq64m1m1rt6m, t133));
            gm_tftb1odc739t5 t135 = t134;
            if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_srcn3jjc71a2bv3, 490, 9);
            gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
        }
        {
            int64_t t137 = gm_l13;
            gm_tarq9lp8tdk6f t138;
            GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t138 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t138 + 24, 0);
            ((uint8_t *)&t138)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t138 + 8), t137);
            gm_tarq9lp8tdk6f t139 = t138;
            uint8_t t140 = ((((uint8_t *)&t139))[0] != 0);
            F.ret = t138;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t140;
            goto gm_ld4_2;
        }
        {
            void * t141 = (void *)((uint8_t *)&F.l6);
            gm_f_deinit_gvn47sj(t141);
        }
        {
            void * t142 = (void *)((uint8_t *)&F.l5);
            gm_f_deinit_gvn47sj(t142);
        }
        goto gm_ls143;
gm_ld4_2:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            void * t144 = (void *)((uint8_t *)&F.l6);
            gm_f_deinit_gvn47sj(t144);
            gm_unw = gm_saved;
        }
gm_ld4_1:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            void * t145 = (void *)((uint8_t *)&F.l5);
            gm_f_deinit_gvn47sj(t145);
            gm_unw = gm_saved;
        }
        goto gm_lad;
gm_ls143:;
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t146 = gm_l0;
        uint64_t t147 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t147 >= (uintptr_t)(t148)) gm_trap_index(t147, t148, gm_srcn3jjc71a2bv3, 493, 21);
        void * t149 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t147 * 8));
        gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t149) + 96));
        uint64_t t151 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t152 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t151 >= (uintptr_t)(t152)) gm_trap_index(t151, t152, gm_srcn3jjc71a2bv3, 493, 33);
        gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t151 * 16));
        gm_b16_8 t154 = t153;
        gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
        int64_t t156 = gm_f_sys_int_32e79ed(t146, GM_CONV(gm_csguankko4lmhd, t150), t155);
        gm_l14 = t156;
    }
    {
        uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t166) { case ((uint8_t)0ULL): goto ma164; case ((uint8_t)1ULL): goto ma164; case ((uint8_t)2ULL): goto ma164; case ((uint8_t)3ULL): goto ma164; case ((uint8_t)4ULL): goto ma164; case ((uint8_t)5ULL): goto ma164; case ((uint8_t)6ULL): goto ma164; case ((uint8_t)7ULL): goto ma164; case ((uint8_t)8ULL): goto ma164; case ((uint8_t)9ULL): goto ma164; case ((uint8_t)10ULL): goto ma164; case ((uint8_t)11ULL): goto ma164; case ((uint8_t)12ULL): goto ma164; case ((uint8_t)13ULL): goto ma164; case ((uint8_t)14ULL): goto ma164; case ((uint8_t)15ULL): goto ma164; case ((uint8_t)16ULL): goto ma164; case ((uint8_t)17ULL): goto ma164; case ((uint8_t)18ULL): goto ma164; case ((uint8_t)19ULL): goto ma164; case ((uint8_t)20ULL): goto ma164; case ((uint8_t)21ULL): goto ma164; case ((uint8_t)22ULL): goto ma164; case ((uint8_t)23ULL): goto ma164; case ((uint8_t)24ULL): goto ma164; case ((uint8_t)25ULL): goto ma164; case ((uint8_t)26ULL): goto ma164; case ((uint8_t)27ULL): goto ma164; case ((uint8_t)28ULL): goto ma164; case ((uint8_t)29ULL): goto ma164; case ((uint8_t)30ULL): goto ma164; case ((uint8_t)31ULL): goto ma164; case ((uint8_t)32ULL): goto ma164; case ((uint8_t)33ULL): goto ma164; case ((uint8_t)34ULL): goto ma164; case ((uint8_t)35ULL): goto ma164; case ((uint8_t)36ULL): goto ma164; case ((uint8_t)37ULL): goto ma164; case ((uint8_t)38ULL): goto ma158; case ((uint8_t)39ULL): goto ma159; case ((uint8_t)40ULL): goto ma160; case ((uint8_t)41ULL): goto ma161; case ((uint8_t)42ULL): goto ma163; case ((uint8_t)43ULL): goto ma162; case ((uint8_t)44ULL): goto ma164; case ((uint8_t)45ULL): goto ma164; case ((uint8_t)46ULL): goto ma164; case ((uint8_t)47ULL): goto ma164; case ((uint8_t)48ULL): goto ma164; case ((uint8_t)49ULL): goto ma164; case ((uint8_t)50ULL): goto ma164; case ((uint8_t)51ULL): goto ma164; case ((uint8_t)52ULL): goto ma164; case ((uint8_t)53ULL): goto ma164; case ((uint8_t)54ULL): goto ma164; default: goto ma164; }
ma158:;
        uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t168 != ((uint8_t)38ULL)) goto mn167;
        {
            void * t170 = gm_l0;
            gm_tl874t2atnlh3 t171 = gm_l1;
            uint64_t t172 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t173 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t172 >= (uintptr_t)(t173)) gm_trap_index(t172, t173, gm_srcn3jjc71a2bv3, 496, 41);
            gm_b16_8 t174 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t172 * 16));
            gm_b16_8 t175 = t174;
            gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)&t175));
            gm_td0ept12e7q8b t177;
            gm_f_sys_src_5svsr6i((void *)&t177, (void *)NULL, cx, NULL, t170, t171, t176);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t178 = t177;
            if (((((uint8_t *)&t178))[0] != 0)) {
                gm_t76123gfet73g t179 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t178) + 8));
                gm_tarq9lp8tdk6f t180;
                GM_ST(uint8_t, (uint8_t *)&t180 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t180 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t180 + 4, 0);
                ((uint8_t *)&t180)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t180 + 8), t179);
                F.ret = t180;
                goto gm_fret;
            }
            gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&t178) + 8));
            gm_l15 = t181;
        }
        {
            void * t182 = gm_l0;
            void * t183 = (void *)((((uint8_t *)t182) + 1352) + 224);
            int64_t t184 = gm_l14;
            gm_tppo2hmut88dq t185 = gm_f_get_fqooi5f(t183, t184);
            gm_tppo2hmut88dq t186 = t185;
            gm_tm98ap4112hee t187;
            if (((((uint8_t *)&t186))[8] != 0)) {
                gm_tm98ap4112hee t188 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t186) + 0));
                t187 = t188;
            } else {
                int64_t t189 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t190;
                GM_ST(uint8_t, (uint8_t *)&t190 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t190 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t190 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t190 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t190 + 24, 0);
                ((uint8_t *)&t190)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t190 + 8), t189);
                gm_tarq9lp8tdk6f t191 = t190;
                uint8_t t192 = ((((uint8_t *)&t191))[0] != 0);
                F.ret = t190;
                goto gm_fret;
            }
            gm_l16 = t187;
        }
        {
            gm_tm98ap4112hee t193 = gm_l16;
            gm_b16_8 t194 = gm_l15;
            gm_node *pn195 = gm_find(cx, 2119230);
            if (!pn195) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 498, 30);
            gm_thf5kr0is9u9i t196;
            t196 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn195->clauses[1])(pn195->env, pn195->hctx, pn195, t193, t194);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t197 = gm_f_sys_count_50c5lcp(t196);
            gm_tarq9lp8tdk6f t198;
            GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t198 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t198 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t198 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t198 + 24, 0);
            ((uint8_t *)&t198)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t198 + 8), t197);
            gm_tarq9lp8tdk6f t199 = t198;
            uint8_t t200 = ((((uint8_t *)&t199))[0] != 0);
            F.ret = t198;
            goto gm_fret;
        }
        goto me157;
mn167:;
ma159:;
        uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t202 != ((uint8_t)39ULL)) goto mn201;
        {
            void * t204 = gm_l0;
            void * t205 = (void *)((((uint8_t *)t204) + 1352) + 224);
            int64_t t206 = gm_l14;
            gm_tppo2hmut88dq t207 = gm_f_get_fqooi5f(t205, t206);
            gm_tppo2hmut88dq t208 = t207;
            gm_tm98ap4112hee t209;
            if (((((uint8_t *)&t208))[8] != 0)) {
                gm_tm98ap4112hee t210 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t208) + 0));
                t209 = t210;
            } else {
                int64_t t211 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t212;
                GM_ST(uint8_t, (uint8_t *)&t212 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t212 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t212 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t212 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t212 + 24, 0);
                ((uint8_t *)&t212)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t212 + 8), t211);
                gm_tarq9lp8tdk6f t213 = t212;
                uint8_t t214 = ((((uint8_t *)&t213))[0] != 0);
                F.ret = t212;
                goto gm_fret;
            }
            gm_l17 = t209;
        }
        {
            gm_tm98ap4112hee t215 = gm_l17;
            gm_node *pn216 = gm_find(cx, 2119230);
            if (!pn216) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 502, 13);
            gm_b0_1 t217;
            t217 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn216->clauses[2])(pn216->env, pn216->hctx, pn216, t215);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tarq9lp8tdk6f t218;
            GM_ST(uint8_t, (uint8_t *)&t218 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t218 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t218 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t218 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t218 + 24, 0);
            ((uint8_t *)&t218)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t218 + 8), ((int64_t)0LL));
            gm_tarq9lp8tdk6f t219 = t218;
            uint8_t t220 = ((((uint8_t *)&t219))[0] != 0);
            F.ret = t218;
            goto gm_fret;
        }
        goto me157;
mn201:;
ma160:;
        uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t222 != ((uint8_t)40ULL)) goto mn221;
        {
            void * t224 = gm_l0;
            gm_tl874t2atnlh3 t225 = gm_l1;
            uint64_t t226 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t227 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t226 >= (uintptr_t)(t227)) gm_trap_index(t226, t227, gm_srcn3jjc71a2bv3, 506, 41);
            gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t226 * 16));
            gm_b16_8 t229 = t228;
            gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&t229));
            gm_td0ept12e7q8b t231;
            gm_f_sys_dest_rtrb14h((void *)&t231, (void *)NULL, cx, NULL, t224, t225, t230);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t232 = t231;
            if (((((uint8_t *)&t232))[0] != 0)) {
                gm_t76123gfet73g t233 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t232) + 8));
                gm_tarq9lp8tdk6f t234;
                GM_ST(uint8_t, (uint8_t *)&t234 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t234 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t234 + 4, 0);
                ((uint8_t *)&t234)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t234 + 8), t233);
                F.ret = t234;
                goto gm_fret;
            }
            gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&t232) + 8));
            gm_l18 = t235;
        }
        {
            void * t236 = gm_l0;
            void * t237 = (void *)((((uint8_t *)t236) + 1352) + 224);
            int64_t t238 = gm_l14;
            gm_tppo2hmut88dq t239 = gm_f_get_fqooi5f(t237, t238);
            gm_tppo2hmut88dq t240 = t239;
            gm_tm98ap4112hee t241;
            if (((((uint8_t *)&t240))[8] != 0)) {
                gm_tm98ap4112hee t242 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t240) + 0));
                t241 = t242;
            } else {
                int64_t t243 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t244;
                GM_ST(uint8_t, (uint8_t *)&t244 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t244 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t244 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t244 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t244 + 24, 0);
                ((uint8_t *)&t244)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t244 + 8), t243);
                gm_tarq9lp8tdk6f t245 = t244;
                uint8_t t246 = ((((uint8_t *)&t245))[0] != 0);
                F.ret = t244;
                goto gm_fret;
            }
            gm_l19 = t241;
        }
        {
            gm_tm98ap4112hee t247 = gm_l19;
            gm_b16_8 t248 = gm_l18;
            gm_node *pn249 = gm_find(cx, 2119230);
            if (!pn249) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 508, 20);
            gm_tlbmih0s72in3 t250;
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn249->clauses[3])((void *)&t250, pn249->env, pn249->hctx, pn249, t247, t248);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tlbmih0s72in3 t251 = t250;
            gm_thdrvp9q0u7b5 t252;
            if (!((((uint8_t *)&t251))[0] != 0)) {
                gm_thdrvp9q0u7b5 t253 = GM_LD(gm_thdrvp9q0u7b5, (((uint8_t *)&t251) + 8));
                t252 = t253;
            } else {
                gm_toe5fmr3vvf4d t254 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t251) + 8));
                gm_l20 = t254;
                gm_toe5fmr3vvf4d t255 = gm_l20;
                int64_t t256 = gm_f_sys_code_cjtlfo1(t255);
                gm_tarq9lp8tdk6f t257;
                GM_ST(uint8_t, (uint8_t *)&t257 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t257 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t257 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t257 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t257 + 24, 0);
                ((uint8_t *)&t257)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t257 + 8), t256);
                gm_tarq9lp8tdk6f t258 = t257;
                uint8_t t259 = ((((uint8_t *)&t258))[0] != 0);
                F.ret = t257;
                goto gm_fret;
            }
            gm_l21 = t252;
        }
        {
            gm_thdrvp9q0u7b5 t260 = gm_l21;
            gm_thdrvp9q0u7b5 t261 = t260;
            gm_te0mgnjdphs16 t262;
            if (((((uint8_t *)&t261))[16] != 0)) {
                gm_te0mgnjdphs16 t263 = GM_LD(gm_te0mgnjdphs16, (((uint8_t *)&t261) + 0));
                t262 = t263;
            } else {
                gm_tarq9lp8tdk6f t264;
                GM_ST(uint8_t, (uint8_t *)&t264 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t264 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t264 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t264 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t264 + 24, 0);
                ((uint8_t *)&t264)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t264 + 8), ((int64_t)0LL));
                gm_tarq9lp8tdk6f t265 = t264;
                uint8_t t266 = ((((uint8_t *)&t265))[0] != 0);
                F.ret = t264;
                goto gm_fret;
            }
            gm_l22 = t262;
        }
        {
            uint64_t t267 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
            int64_t t268 = (int64_t)t267;
            int64_t t269;
            uint8_t t271 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0) + 0) == ((uint8_t)1ULL)));
            if (!t271) goto ie270;
            unsigned __int128 n273 = (unsigned __int128)((uint64_t)32ULL);
            if (n273 >= 64) gm_trap_shift((uint64_t)n273, "i64", gm_srcn3jjc71a2bv3, 510, 71);
            int64_t r274 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n273);
            bool o275 = (int64_t)(r274 >> (int)n273) != ((int64_t)1LL);
            if (o275) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 510, 71);
            int64_t t276 = r274;
            int64_t t277 = t276;
            t269 = t277;
            goto ix270;
ie270:;
            int64_t t279 = ((int64_t)0LL);
            t269 = t279;
ix270:;
            int64_t r280; bool o281 = __builtin_add_overflow(t268, t269, &r280);
            if (o281) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 510, 20);
            int64_t t282 = r280;
            gm_tarq9lp8tdk6f t283;
            GM_ST(uint8_t, (uint8_t *)&t283 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t283 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t283 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t283 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t283 + 24, 0);
            ((uint8_t *)&t283)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t283 + 8), t282);
            gm_tarq9lp8tdk6f t284 = t283;
            uint8_t t285 = ((((uint8_t *)&t284))[0] != 0);
            F.ret = t283;
            goto gm_fret;
        }
        goto me157;
mn221:;
ma161:;
        uint8_t t287 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t287 != ((uint8_t)41ULL)) goto mn286;
        {
            void * t289 = gm_l0;
            void * t290 = (void *)((((uint8_t *)t289) + 1352) + 224);
            int64_t t291 = gm_l14;
            gm_tppo2hmut88dq t292 = gm_f_get_fqooi5f(t290, t291);
            gm_tppo2hmut88dq t293 = t292;
            gm_tm98ap4112hee t294;
            if (((((uint8_t *)&t293))[8] != 0)) {
                gm_tm98ap4112hee t295 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t293) + 0));
                t294 = t295;
            } else {
                int64_t t296 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t297;
                GM_ST(uint8_t, (uint8_t *)&t297 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t297 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t297 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t297 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t297 + 24, 0);
                ((uint8_t *)&t297)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t297 + 8), t296);
                gm_tarq9lp8tdk6f t298 = t297;
                uint8_t t299 = ((((uint8_t *)&t298))[0] != 0);
                F.ret = t297;
                goto gm_fret;
            }
            gm_l23 = t294;
        }
        {
            gm_tm98ap4112hee t300 = gm_l23;
            gm_node *pn301 = gm_find(cx, 2119230);
            if (!pn301) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 514, 18);
            gm_tlmjd3nndlf3r t302;
            t302 = ((gm_tlmjd3nndlf3r (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn301->clauses[4])(pn301->env, pn301->hctx, pn301, t300);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t302;
        }
        {
            {
                void * t304 = gm_l0;
                void * t305 = (void *)((((uint8_t *)t304) + 1352) + 224);
                int64_t t306 = gm_l14;
                gm_tppo2hmut88dq t307 = gm_f_remove_jvpr521(t305, t306);
            }
        }
        {
            gm_tlmjd3nndlf3r t308 = gm_l24;
            gm_tlmjd3nndlf3r t309 = t308;
            gm_tsoiri44d0i6q t310;
            if (!((((uint8_t *)&t309))[0] != 0)) {
                gm_tsoiri44d0i6q t311 = GM_LD(gm_tsoiri44d0i6q, (((uint8_t *)&t309) + 4));
                t310 = t311;
            } else {
                gm_toe5fmr3vvf4d t312 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t309) + 4));
                gm_l25 = t312;
                gm_toe5fmr3vvf4d t313 = gm_l25;
                int64_t t314 = gm_f_sys_code_cjtlfo1(t313);
                gm_tarq9lp8tdk6f t315;
                GM_ST(uint8_t, (uint8_t *)&t315 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t315 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t315 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t315 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t315 + 24, 0);
                ((uint8_t *)&t315)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t315 + 8), t314);
                gm_tarq9lp8tdk6f t316 = t315;
                uint8_t t317 = ((((uint8_t *)&t316))[0] != 0);
                F.ret = t315;
                goto gm_fret;
            }
            gm_l26 = t310;
        }
        {
            gm_tarq9lp8tdk6f t318;
            uint8_t t321 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
            if (t321 != ((uint8_t)0ULL)) goto mn320;
            uint32_t t322 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 4));
            gm_l27 = t322;
            uint32_t t323 = gm_l27;
            int64_t t324 = (int64_t)t323;
            gm_tarq9lp8tdk6f t325;
            GM_ST(uint8_t, (uint8_t *)&t325 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t325 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t325 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t325 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t325 + 24, 0);
            ((uint8_t *)&t325)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t325 + 8), t324);
            t318 = t325;
            goto me319;
mn320:;
            uint8_t t327 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
            if (t327 != ((uint8_t)1ULL)) goto mn326;
            int32_t t328 = GM_LD(int32_t, (((uint8_t *)&gm_l26) + 4));
            gm_l28 = t328;
            int32_t t329 = gm_l28;
            int64_t t330 = (int64_t)t329;
            unsigned __int128 n331 = (unsigned __int128)((uint64_t)32ULL);
            if (n331 >= 64) gm_trap_shift((uint64_t)n331, "i64", gm_srcn3jjc71a2bv3, 519, 47);
            int64_t r332 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n331);
            bool o333 = (int64_t)(r332 >> (int)n331) != ((int64_t)1LL);
            if (o333) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 519, 47);
            int64_t t334 = r332;
            int64_t r335; bool o336 = __builtin_add_overflow(t330, t334, &r335);
            if (o336) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 519, 35);
            int64_t t337 = r335;
            gm_tarq9lp8tdk6f t338;
            GM_ST(uint8_t, (uint8_t *)&t338 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t338 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t338 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t338 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t338 + 24, 0);
            ((uint8_t *)&t338)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t338 + 8), t337);
            t318 = t338;
            goto me319;
mn326:;
            gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 517, 20);
me319:;
            gm_tarq9lp8tdk6f t339 = t318;
            uint8_t t340 = ((((uint8_t *)&t339))[0] != 0);
            F.ret = t318;
            goto gm_fret;
        }
        goto me157;
mn286:;
ma162:;
        uint8_t t342 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t342 != ((uint8_t)43ULL)) goto mn341;
        {
            void * t344 = gm_l0;
            void * t345 = (void *)((((uint8_t *)t344) + 1352) + 224);
            int64_t t346 = gm_l14;
            gm_tppo2hmut88dq t347 = gm_f_get_fqooi5f(t345, t346);
            gm_tppo2hmut88dq t348 = t347;
            gm_tm98ap4112hee t349;
            if (((((uint8_t *)&t348))[8] != 0)) {
                gm_tm98ap4112hee t350 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t348) + 0));
                t349 = t350;
            } else {
                int64_t t351 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t352;
                GM_ST(uint8_t, (uint8_t *)&t352 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t352 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t352 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t352 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t352 + 24, 0);
                ((uint8_t *)&t352)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t352 + 8), t351);
                gm_tarq9lp8tdk6f t353 = t352;
                uint8_t t354 = ((((uint8_t *)&t353))[0] != 0);
                F.ret = t352;
                goto gm_fret;
            }
            gm_l29 = t349;
        }
        {
            gm_tm98ap4112hee t355 = gm_l29;
            gm_node *pn356 = gm_find(cx, 2119230);
            if (!pn356) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 524, 18);
            gm_tr6norheal11n t357;
            t357 = ((gm_tr6norheal11n (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn356->clauses[5])(pn356->env, pn356->hctx, pn356, t355);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t357;
        }
        {
            gm_tr6norheal11n t358 = gm_l30;
            gm_tr6norheal11n t359 = t358;
            gm_tqnlpelep1582 t360;
            if (!((((uint8_t *)&t359))[0] != 0)) {
                gm_tqnlpelep1582 t361 = GM_LD(gm_tqnlpelep1582, (((uint8_t *)&t359) + 4));
                t360 = t361;
            } else {
                gm_toe5fmr3vvf4d t362 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t359) + 4));
                gm_l31 = t362;
                gm_toe5fmr3vvf4d t363 = gm_l31;
                int64_t t364 = gm_f_sys_code_cjtlfo1(t363);
                gm_tarq9lp8tdk6f t365;
                GM_ST(uint8_t, (uint8_t *)&t365 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t365 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t365 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t365 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t365 + 24, 0);
                ((uint8_t *)&t365)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t365 + 8), t364);
                gm_tarq9lp8tdk6f t366 = t365;
                uint8_t t367 = ((((uint8_t *)&t366))[0] != 0);
                F.ret = t365;
                goto gm_fret;
            }
            gm_l32 = t360;
        }
        {
            gm_tqnlpelep1582 t368 = gm_l32;
            gm_tqnlpelep1582 t369 = t368;
            gm_tsoiri44d0i6q t370;
            if (((((uint8_t *)&t369))[8] != 0)) {
                gm_tsoiri44d0i6q t371 = GM_LD(gm_tsoiri44d0i6q, (((uint8_t *)&t369) + 0));
                t370 = t371;
            } else {
                unsigned __int128 n372 = (unsigned __int128)((uint64_t)33ULL);
                if (n372 >= 64) gm_trap_shift((uint64_t)n372, "i64", gm_srcn3jjc71a2bv3, 526, 39);
                int64_t r373 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n372);
                bool o374 = (int64_t)(r373 >> (int)n372) != ((int64_t)1LL);
                if (o374) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 526, 39);
                int64_t t375 = r373;
                gm_tarq9lp8tdk6f t376;
                GM_ST(uint8_t, (uint8_t *)&t376 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t376 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t376 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t376 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t376 + 24, 0);
                ((uint8_t *)&t376)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t376 + 8), t375);
                gm_tarq9lp8tdk6f t377 = t376;
                uint8_t t378 = ((((uint8_t *)&t377))[0] != 0);
                F.ret = t376;
                goto gm_fret;
            }
            gm_l33 = t370;
        }
        {
            {
                void * t380 = gm_l0;
                void * t381 = (void *)((((uint8_t *)t380) + 1352) + 224);
                int64_t t382 = gm_l14;
                gm_tppo2hmut88dq t383 = gm_f_remove_jvpr521(t381, t382);
            }
        }
        {
            gm_tarq9lp8tdk6f t384;
            uint8_t t387 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
            if (t387 != ((uint8_t)0ULL)) goto mn386;
            uint32_t t388 = GM_LD(uint32_t, (((uint8_t *)&gm_l33) + 4));
            gm_l34 = t388;
            uint32_t t389 = gm_l34;
            int64_t t390 = (int64_t)t389;
            gm_tarq9lp8tdk6f t391;
            GM_ST(uint8_t, (uint8_t *)&t391 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t391 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t391 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t391 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t391 + 24, 0);
            ((uint8_t *)&t391)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t391 + 8), t390);
            t384 = t391;
            goto me385;
mn386:;
            uint8_t t393 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
            if (t393 != ((uint8_t)1ULL)) goto mn392;
            int32_t t394 = GM_LD(int32_t, (((uint8_t *)&gm_l33) + 4));
            gm_l35 = t394;
            int32_t t395 = gm_l35;
            int64_t t396 = (int64_t)t395;
            unsigned __int128 n397 = (unsigned __int128)((uint64_t)32ULL);
            if (n397 >= 64) gm_trap_shift((uint64_t)n397, "i64", gm_srcn3jjc71a2bv3, 530, 47);
            int64_t r398 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n397);
            bool o399 = (int64_t)(r398 >> (int)n397) != ((int64_t)1LL);
            if (o399) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 530, 47);
            int64_t t400 = r398;
            int64_t r401; bool o402 = __builtin_add_overflow(t396, t400, &r401);
            if (o402) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 530, 35);
            int64_t t403 = r401;
            gm_tarq9lp8tdk6f t404;
            GM_ST(uint8_t, (uint8_t *)&t404 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t404 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t404 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t404 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t404 + 24, 0);
            ((uint8_t *)&t404)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t404 + 8), t403);
            t384 = t404;
            goto me385;
mn392:;
            gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 528, 20);
me385:;
            gm_tarq9lp8tdk6f t405 = t384;
            uint8_t t406 = ((((uint8_t *)&t405))[0] != 0);
            F.ret = t384;
            goto gm_fret;
        }
        goto me157;
mn341:;
ma163:;
        uint8_t t408 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t408 != ((uint8_t)42ULL)) goto mn407;
        {
            void * t410 = gm_l0;
            void * t411 = (void *)((((uint8_t *)t410) + 1352) + 224);
            int64_t t412 = gm_l14;
            gm_tppo2hmut88dq t413 = gm_f_get_fqooi5f(t411, t412);
            gm_tppo2hmut88dq t414 = t413;
            gm_tm98ap4112hee t415;
            if (((((uint8_t *)&t414))[8] != 0)) {
                gm_tm98ap4112hee t416 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t414) + 0));
                t415 = t416;
            } else {
                int64_t t417 = GM_LD(int64_t, gm_c3qosqjl81k);
                gm_tarq9lp8tdk6f t418;
                GM_ST(uint8_t, (uint8_t *)&t418 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t418 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t418 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t418 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t418 + 24, 0);
                ((uint8_t *)&t418)[0] = 0;
                GM_ST(int64_t, ((uint8_t *)&t418 + 8), t417);
                gm_tarq9lp8tdk6f t419 = t418;
                uint8_t t420 = ((((uint8_t *)&t419))[0] != 0);
                F.ret = t418;
                goto gm_fret;
            }
            gm_l36 = t415;
        }
        {
            gm_tm98ap4112hee t421 = gm_l36;
            gm_node *pn422 = gm_find(cx, 2119230);
            if (!pn422) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 535, 29);
            gm_ttln0d0hd70en t423;
            t423 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn422->clauses[6])(pn422->env, pn422->hctx, pn422, t421);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t424 = gm_f_sys_unit_nrsqciq(t423);
            gm_tarq9lp8tdk6f t425;
            GM_ST(uint8_t, (uint8_t *)&t425 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t425 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t425 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t425 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t425 + 24, 0);
            ((uint8_t *)&t425)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t425 + 8), t424);
            gm_tarq9lp8tdk6f t426 = t425;
            uint8_t t427 = ((((uint8_t *)&t426))[0] != 0);
            F.ret = t425;
            goto gm_fret;
        }
        goto me157;
mn407:;
ma164:;
        void * t429 = gm_l0;
        gm_tl874t2atnlh3 t430 = gm_l1;
        gm_b16_8 t431;
        GM_ST(void *, (uint8_t *)&t431, (void *)(gm_stfd80s46313k));
        GM_ST(uintptr_t, (uint8_t *)&t431 + 8, (uintptr_t)(37));
        gm_t76123gfet73g t432;
        gm_f_trap_qh84l9d((void *)&t432, (void *)NULL, cx, NULL, t429, t430, t431);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tarq9lp8tdk6f t433;
        GM_ST(uint8_t, (uint8_t *)&t433 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t433 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t433 + 4, 0);
        ((uint8_t *)&t433)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t433 + 8), t432);
        gm_tarq9lp8tdk6f t434 = t433;
        uint8_t t435 = ((((uint8_t *)&t434))[0] != 0);
        F.ret = t433;
        goto gm_fret;
        goto me157;
mn428:;
ma165:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 494, 5);
me157:;
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
inline __attribute__((aligned(4))) void gm_f_sys_read_b0gj9fg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_read_b0gj9fg F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* vals */
    GM_STACK_CHECK(&F, 767u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_srcn3jjc71a2bv3, 204, 33);
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t4 * 16));
        gm_b16_8 t7 = t6;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_td0ept12e7q8b t9;
        gm_f_sys_dest_rtrb14h((void *)&t9, (void *)NULL, cx, NULL, t2, t3, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_td0ept12e7q8b t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) {
            gm_t76123gfet73g t11 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t10) + 8));
            gm_tarq9lp8tdk6f t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            ((uint8_t *)&t12)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t12 + 8), t11);
            F.ret = t12;
            goto gm_fret;
        }
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
        F.l3 = t13;
    }
    {
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((((uint8_t *)t15) + 1656) + 0) + 0) + 8);
        uint8_t t17 = (t16 > ((uint64_t)1ULL));
        if (!t17) goto ie14;
        {
            void * t19 = gm_l0;
            gm_tl874t2atnlh3 t20 = gm_l1;
            gm_b16_8 t21 = F.l3;
            gm_tmvdbo28o1i6h t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t22 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t22 + 8), t21);
            gm_t6j8vdmi81h7c t23;
            gm_f_on_host_thread_kmegc07((void *)&t23, (void *)NULL, cx, NULL, t19, t20, &t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j8vdmi81h7c t24 = t23;
            if (((((uint8_t *)&t24))[0] != 0)) {
                gm_t76123gfet73g t25 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t24) + 8));
                gm_tarq9lp8tdk6f t26;
                GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                ((uint8_t *)&t26)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t26 + 8), t25);
                F.ret = t26;
                goto gm_fret;
            }
            gm_tfvg1o4f8mmhi t27 = GM_LD(gm_tfvg1o4f8mmhi, (((uint8_t *)&t24) + 8));
            gm_tfvg1o4f8mmhi t28 = t27;
            gm_thf5kr0is9u9i t29 = GM_LD(gm_thf5kr0is9u9i, (((uint8_t *)&t28) + 0));
            int64_t t30 = gm_f_sys_count_50c5lcp(t29);
            gm_tarq9lp8tdk6f t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 24, 0);
            ((uint8_t *)&t31)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t31 + 8), t30);
            gm_tarq9lp8tdk6f t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        gm_toe5fmr3vvf4d t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)12ULL));
        gm_thf5kr0is9u9i t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        ((uint8_t *)&t35)[0] = 1;
        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t35 + 8), t34);
        F.l4 = t35;
    }
    {
        gm_closure t36 = ((gm_closure){(void *)gm_f_fn_trt04qi, (void *)&F.h, cx});
        gm_f_defaults_p077jgf(t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_thf5kr0is9u9i t37 = F.l4;
        int64_t t38 = gm_f_sys_count_50c5lcp(t37);
        gm_tarq9lp8tdk6f t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t39 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t39 + 24, 0);
        ((uint8_t *)&t39)[0] = 0;
        GM_ST(int64_t, ((uint8_t *)&t39 + 8), t38);
        gm_tarq9lp8tdk6f t40 = t39;
        uint8_t t41 = ((((uint8_t *)&t40))[0] != 0);
        F.ret = t39;
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
extern __attribute__((aligned(4))) void gm_f_sys_ret_vpa2k8i(void * a0, gm_b16_8 a1, int64_t a2);
__attribute__((aligned(4))) void gm_f_sys_run_2vqjo3u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_sys_run_2vqjo3u F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l5; /* dst */
    int64_t gm_l6; /* n */
    uint64_t gm_l7; /* now */
    uint64_t gm_l8; /* base */
    gm_b16_8 gm_l9; /* buf */
    gm_b16_8 gm_l10; /* data */
    GM_STACK_CHECK(&F, 2455u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        {
            gm_l6 = ((int64_t)0LL);
        }
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t19) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma12; case ((uint8_t)29ULL): goto ma12; case ((uint8_t)30ULL): goto ma12; case ((uint8_t)31ULL): goto ma12; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma12; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma12; case ((uint8_t)36ULL): goto ma12; case ((uint8_t)37ULL): goto ma13; case ((uint8_t)38ULL): goto ma13; case ((uint8_t)39ULL): goto ma13; case ((uint8_t)40ULL): goto ma13; case ((uint8_t)41ULL): goto ma13; case ((uint8_t)42ULL): goto ma13; case ((uint8_t)43ULL): goto ma13; case ((uint8_t)44ULL): goto ma14; case ((uint8_t)45ULL): goto ma15; case ((uint8_t)46ULL): goto ma16; case ((uint8_t)47ULL): goto ma17; case ((uint8_t)48ULL): goto ma17; case ((uint8_t)49ULL): goto ma17; case ((uint8_t)50ULL): goto ma17; case ((uint8_t)51ULL): goto ma17; case ((uint8_t)52ULL): goto ma17; case ((uint8_t)53ULL): goto ma10; case ((uint8_t)54ULL): goto ma9; default: goto ma17; }
ma4:;
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t21 != ((uint8_t)0ULL)) goto mn20;
            {
                void * t23 = gm_l0;
                gm_tl874t2atnlh3 t24 = gm_l1;
                gm_b16_8 t25 = gm_l4;
                gm_tarq9lp8tdk6f t26;
                gm_f_sys_read_b0gj9fg((void *)&t26, (void *)NULL, cx, NULL, t23, t24, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tarq9lp8tdk6f t27 = t26;
                if (((((uint8_t *)&t27))[0] != 0)) {
                    gm_t76123gfet73g t28 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t27) + 8));
                    gm_tcfa6l7reoosn t29;
                    GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                    ((uint8_t *)&t29)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t29 + 8), t28);
                    F.ret = t29;
                    goto gm_fret;
                }
                int64_t t30 = GM_LD(int64_t, (((uint8_t *)&t27) + 8));
                gm_l6 = t30;
            }
            goto me3;
mn20:;
ma5:;
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t32 != ((uint8_t)1ULL)) goto mn31;
            {
                gm_node *pn34 = gm_find(cx, 1579066750);
                if (!pn34) gm_trap("no handler installed for effect `Clock`", gm_srcn3jjc71a2bv3, 170, 20);
                gm_t4ce0jd3tdro4 t35;
                t35 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn34->clauses[0])(pn34->env, pn34->hctx, pn34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t4ce0jd3tdro4 t36 = t35;
                uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&t36) + 0));
                gm_l7 = t37;
            }
            {
                void * t39 = gm_l0;
                uint8_t t40;
                if (((((((uint8_t *)t39) + 1352) + 288))[8] != 0)) goto mn42;
                t40 = ((uint8_t)1);
                goto me41;
mn42:;
                t40 = ((uint8_t)0);
                goto me41;
mn43:;
                gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 171, 16);
me41:;
                if (!t40) goto ie38;
                {
                    void * t45 = gm_l0;
                    uint64_t t46 = gm_l7;
                    gm_tf5srh4mp4b3i t47;
                    GM_ST(uint8_t, (uint8_t *)&t47 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t47 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t47 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t47), t46);
                    ((uint8_t *)&t47)[8] = 1;
                    GM_ST(gm_tf5srh4mp4b3i, ((((uint8_t *)t45) + 1352) + 288), t47);
                }
                goto ix38;
ie38:;
ix38:;
            }
            {
                void * t48 = gm_l0;
                gm_tf5srh4mp4b3i t49 = GM_LD(gm_tf5srh4mp4b3i, ((((uint8_t *)t48) + 1352) + 288));
                gm_tf5srh4mp4b3i t50 = t49;
                uint64_t t51;
                if (((((uint8_t *)&t50))[8] != 0)) {
                    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&t50) + 0));
                    t51 = t52;
                } else {
                    t51 = ((uint64_t)0ULL);
                }
                gm_l8 = t51;
            }
            {
                void * t53 = gm_l0;
                gm_b16_8 t54 = gm_l5;
                uint64_t t55 = gm_l7;
                uint64_t t56 = gm_l8;
                uint64_t r57; bool o58 = __builtin_sub_overflow(t55, t56, &r57);
                uint64_t t59 = r57;
                unsigned __int128 t60 = (unsigned __int128)t59;
                gm_f_put_uint_cddin99(t53, t54, t60);
            }
            {
                gm_b0_1 t61;
                gm_tcfa6l7reoosn t62;
                GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t62 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t62 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t62 + 24, 0);
                ((uint8_t *)&t62)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t62 + 8), t61);
                gm_tcfa6l7reoosn t63 = t62;
                uint8_t t64 = ((((uint8_t *)&t63))[0] != 0);
                F.ret = t62;
                goto gm_fret;
            }
            goto me3;
mn31:;
ma6:;
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t66 != ((uint8_t)2ULL)) goto mn65;
            {
                gm_node *pn68 = gm_find(cx, 1579066750);
                if (!pn68) gm_trap("no handler installed for effect `Clock`", gm_srcn3jjc71a2bv3, 176, 29);
                int64_t t69;
                t69 = ((int64_t (*)(void *, gm_node *, gm_node *))pn68->clauses[1])(pn68->env, pn68->hctx, pn68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t69;
            }
            goto me3;
mn65:;
ma7:;
            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t71 != ((uint8_t)3ULL)) goto mn70;
            {
                void * t73 = gm_l0;
                gm_tl874t2atnlh3 t74 = gm_l1;
                void * t75 = gm_l0;
                uint64_t t76 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_srcn3jjc71a2bv3, 178, 45);
                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t76 * 16));
                gm_b16_8 t79 = t78;
                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                unsigned __int128 t81 = gm_f_get_uint_bq8c4b7(t75, t80);
                uint64_t t82 = (uint64_t)t81;
                gm_f_sleep_gqtie06((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t73, t74, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto me3;
mn70:;
ma8:;
            uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t84 != ((uint8_t)4ULL)) goto mn83;
            {
                void * t86 = gm_l0;
                gm_tl874t2atnlh3 t87 = gm_l1;
                uint64_t t88 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t88 >= (uintptr_t)(t89)) gm_trap_index(t88, t89, gm_srcn3jjc71a2bv3, 181, 41);
                gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t88 * 16));
                gm_b16_8 t91 = t90;
                gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                gm_td0ept12e7q8b t93;
                gm_f_sys_dest_rtrb14h((void *)&t93, (void *)NULL, cx, NULL, t86, t87, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_td0ept12e7q8b t94 = t93;
                if (((((uint8_t *)&t94))[0] != 0)) {
                    gm_t76123gfet73g t95 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t94) + 8));
                    gm_tcfa6l7reoosn t96;
                    GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t96 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t96 + 4, 0);
                    ((uint8_t *)&t96)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t96 + 8), t95);
                    F.ret = t96;
                    goto gm_fret;
                }
                gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&t94) + 8));
                gm_l9 = t97;
            }
            {
                gm_b16_8 t98 = gm_l9;
                gm_node *pn99 = gm_find(cx, 1087599154);
                if (!pn99) gm_trap("no handler installed for effect `Rand`", gm_srcn3jjc71a2bv3, 182, 13);
                gm_b0_1 t100;
                t100 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn99->clauses[0])(pn99->env, pn99->hctx, pn99, t98);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t101;
                gm_tcfa6l7reoosn t102;
                GM_ST(uint8_t, (uint8_t *)&t102 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t102 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t102 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t102 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t102 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t102 + 24, 0);
                ((uint8_t *)&t102)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t102 + 8), t101);
                gm_tcfa6l7reoosn t103 = t102;
                uint8_t t104 = ((((uint8_t *)&t103))[0] != 0);
                F.ret = t102;
                goto gm_fret;
            }
            goto me3;
mn83:;
ma9:;
            uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t106 != ((uint8_t)54ULL)) goto mn105;
            {
                void * t108 = gm_l0;
                gm_tl874t2atnlh3 t109 = gm_l1;
                uint64_t t110 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t110 >= (uintptr_t)(t111)) gm_trap_index(t110, t111, gm_srcn3jjc71a2bv3, 186, 41);
                gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t110 * 16));
                gm_b16_8 t113 = t112;
                gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                gm_td0ept12e7q8b t115;
                gm_f_sys_src_5svsr6i((void *)&t115, (void *)NULL, cx, NULL, t108, t109, t114);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_td0ept12e7q8b t116 = t115;
                if (((((uint8_t *)&t116))[0] != 0)) {
                    gm_t76123gfet73g t117 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t116) + 8));
                    gm_tcfa6l7reoosn t118;
                    GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
                    ((uint8_t *)&t118)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t118 + 8), t117);
                    F.ret = t118;
                    goto gm_fret;
                }
                gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&t116) + 8));
                gm_l10 = t119;
            }
            {
                void * t120 = gm_l0;
                uint32_t t121;
                void * t123 = gm_l0;
                uint64_t t124 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t125 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t124 >= (uintptr_t)(t125)) gm_trap_index(t124, t125, gm_srcn3jjc71a2bv3, 187, 40);
                void * t126 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t124 * 8));
                gm_t88rcucg1b7uk t127 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t126) + 96));
                uint64_t t128 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t128 >= (uintptr_t)(t129)) gm_trap_index(t128, t129, gm_srcn3jjc71a2bv3, 187, 52);
                gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t128 * 16));
                gm_b16_8 t131 = t130;
                gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
                int64_t t133 = gm_f_sys_int_32e79ed(t123, GM_CONV(gm_csguankko4lmhd, t127), t132);
                uint8_t t134 = ((t133 == ((int64_t)2LL)));
                if (!t134) goto ie122;
                uint32_t t136 = ((uint32_t)2ULL);
                t121 = t136;
                goto ix122;
ie122:;
                uint32_t t138 = ((uint32_t)1ULL);
                t121 = t138;
ix122:;
                gm_b16_8 t139 = gm_l10;
                gm_b0_1 t140 = gm_f_host_write_r26no87((void *)NULL, cx, NULL, t120, t121, t139);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t141;
                gm_tcfa6l7reoosn t142;
                GM_ST(uint8_t, (uint8_t *)&t142 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t142 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t142 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t142 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t142 + 24, 0);
                ((uint8_t *)&t142)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t142 + 8), t141);
                gm_tcfa6l7reoosn t143 = t142;
                uint8_t t144 = ((((uint8_t *)&t143))[0] != 0);
                F.ret = t142;
                goto gm_fret;
            }
            goto me3;
mn105:;
ma10:;
            uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t146 != ((uint8_t)53ULL)) goto mn145;
            {
                uint32_t t148 = gm_f_cpu_count_0o5882g();
                int64_t t149 = (int64_t)t148;
                gm_l6 = t149;
            }
            goto me3;
mn145:;
ma11:;
            uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t153 != ((uint8_t)5ULL)) goto pa152;
            goto po151;
pa152:;
            uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t155 != ((uint8_t)6ULL)) goto pa154;
            goto po151;
pa154:;
            uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t157 != ((uint8_t)7ULL)) goto pa156;
            goto po151;
pa156:;
            uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t159 != ((uint8_t)8ULL)) goto pa158;
            goto po151;
pa158:;
            uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t161 != ((uint8_t)9ULL)) goto pa160;
            goto po151;
pa160:;
            uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t163 != ((uint8_t)10ULL)) goto pa162;
            goto po151;
pa162:;
            uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t165 != ((uint8_t)11ULL)) goto pa164;
            goto po151;
pa164:;
            uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t167 != ((uint8_t)12ULL)) goto pa166;
            goto po151;
pa166:;
            uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t169 != ((uint8_t)13ULL)) goto pa168;
            goto po151;
pa168:;
            uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t171 != ((uint8_t)14ULL)) goto pa170;
            goto po151;
pa170:;
            uint8_t t173 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t173 != ((uint8_t)15ULL)) goto pa172;
            goto po151;
pa172:;
            uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t175 != ((uint8_t)16ULL)) goto pa174;
            goto po151;
pa174:;
            uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t177 != ((uint8_t)17ULL)) goto pa176;
            goto po151;
pa176:;
            uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t179 != ((uint8_t)18ULL)) goto pa178;
            goto po151;
pa178:;
            uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t181 != ((uint8_t)19ULL)) goto pa180;
            goto po151;
pa180:;
            uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t183 != ((uint8_t)20ULL)) goto pa182;
            goto po151;
pa182:;
            uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t185 != ((uint8_t)21ULL)) goto pa184;
            goto po151;
pa184:;
            uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t187 != ((uint8_t)22ULL)) goto pa186;
            goto po151;
pa186:;
            uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t189 != ((uint8_t)23ULL)) goto pa188;
            goto po151;
pa188:;
            uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t191 != ((uint8_t)24ULL)) goto pa190;
            goto po151;
pa190:;
            uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t193 != ((uint8_t)25ULL)) goto pa192;
            goto po151;
pa192:;
            uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t195 != ((uint8_t)26ULL)) goto pa194;
            goto po151;
pa194:;
            goto mn150;
po151:;
            {
                void * t197 = gm_l0;
                gm_tl874t2atnlh3 t198 = gm_l1;
                gm_t9qkh0rsooc8d t199 = gm_l2;
                gm_b16_8 t200 = gm_l3;
                gm_b16_8 t201 = gm_l4;
                gm_tarq9lp8tdk6f t202;
                gm_f_sys_fs_u0grkf6((void *)&t202, (void *)NULL, cx, NULL, t197, t198, t199, t200, t201);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tarq9lp8tdk6f t203 = t202;
                if (((((uint8_t *)&t203))[0] != 0)) {
                    gm_t76123gfet73g t204 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t203) + 8));
                    gm_tcfa6l7reoosn t205;
                    GM_ST(uint8_t, (uint8_t *)&t205 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t205 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t205 + 4, 0);
                    ((uint8_t *)&t205)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t205 + 8), t204);
                    F.ret = t205;
                    goto gm_fret;
                }
                int64_t t206 = GM_LD(int64_t, (((uint8_t *)&t203) + 8));
                gm_l6 = t206;
            }
            goto me3;
mn150:;
ma12:;
            uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t210 != ((uint8_t)27ULL)) goto pa209;
            goto po208;
pa209:;
            uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t212 != ((uint8_t)28ULL)) goto pa211;
            goto po208;
pa211:;
            uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t214 != ((uint8_t)29ULL)) goto pa213;
            goto po208;
pa213:;
            uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t216 != ((uint8_t)30ULL)) goto pa215;
            goto po208;
pa215:;
            uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t218 != ((uint8_t)31ULL)) goto pa217;
            goto po208;
pa217:;
            uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t220 != ((uint8_t)32ULL)) goto pa219;
            goto po208;
pa219:;
            uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t222 != ((uint8_t)33ULL)) goto pa221;
            goto po208;
pa221:;
            uint8_t t224 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t224 != ((uint8_t)34ULL)) goto pa223;
            goto po208;
pa223:;
            uint8_t t226 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t226 != ((uint8_t)35ULL)) goto pa225;
            goto po208;
pa225:;
            uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t228 != ((uint8_t)36ULL)) goto pa227;
            goto po208;
pa227:;
            goto mn207;
po208:;
            {
                void * t230 = gm_l0;
                gm_tl874t2atnlh3 t231 = gm_l1;
                gm_t9qkh0rsooc8d t232 = gm_l2;
                gm_b16_8 t233 = gm_l3;
                gm_b16_8 t234 = gm_l4;
                gm_tarq9lp8tdk6f t235;
                gm_f_sys_net_aff7h5l((void *)&t235, (void *)NULL, cx, NULL, t230, t231, t232, t233, t234);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tarq9lp8tdk6f t236 = t235;
                if (((((uint8_t *)&t236))[0] != 0)) {
                    gm_t76123gfet73g t237 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t236) + 8));
                    gm_tcfa6l7reoosn t238;
                    GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
                    ((uint8_t *)&t238)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t238 + 8), t237);
                    F.ret = t238;
                    goto gm_fret;
                }
                int64_t t239 = GM_LD(int64_t, (((uint8_t *)&t236) + 8));
                gm_l6 = t239;
            }
            goto me3;
mn207:;
ma13:;
            uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t243 != ((uint8_t)37ULL)) goto pa242;
            goto po241;
pa242:;
            uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t245 != ((uint8_t)38ULL)) goto pa244;
            goto po241;
pa244:;
            uint8_t t247 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t247 != ((uint8_t)39ULL)) goto pa246;
            goto po241;
pa246:;
            uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t249 != ((uint8_t)40ULL)) goto pa248;
            goto po241;
pa248:;
            uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t251 != ((uint8_t)41ULL)) goto pa250;
            goto po241;
pa250:;
            uint8_t t253 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t253 != ((uint8_t)43ULL)) goto pa252;
            goto po241;
pa252:;
            uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t255 != ((uint8_t)42ULL)) goto pa254;
            goto po241;
pa254:;
            goto mn240;
po241:;
            {
                void * t257 = gm_l0;
                gm_tl874t2atnlh3 t258 = gm_l1;
                gm_t9qkh0rsooc8d t259 = gm_l2;
                gm_b16_8 t260 = gm_l3;
                gm_b16_8 t261 = gm_l4;
                gm_tarq9lp8tdk6f t262;
                gm_f_sys_proc_jhdp277((void *)&t262, (void *)NULL, cx, NULL, t257, t258, t259, t260, t261);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tarq9lp8tdk6f t263 = t262;
                if (((((uint8_t *)&t263))[0] != 0)) {
                    gm_t76123gfet73g t264 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t263) + 8));
                    gm_tcfa6l7reoosn t265;
                    GM_ST(uint8_t, (uint8_t *)&t265 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t265 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t265 + 4, 0);
                    ((uint8_t *)&t265)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t265 + 8), t264);
                    F.ret = t265;
                    goto gm_fret;
                }
                int64_t t266 = GM_LD(int64_t, (((uint8_t *)&t263) + 8));
                gm_l6 = t266;
            }
            goto me3;
mn240:;
ma14:;
            uint8_t t268 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t268 != ((uint8_t)44ULL)) goto mn267;
            {
                void * t270 = gm_l0;
                gm_tl874t2atnlh3 t271 = gm_l1;
                gm_t9qkh0rsooc8d t272;
                GM_ST(uint8_t, ((uint8_t *)&t272 + 0), ((uint8_t)2ULL));
                gm_b16_8 t273 = gm_f_empty_slice_2p7d2kv();
                gm_b16_8 t274 = t273;
                gm_b16_8 t275 = GM_LD(gm_b16_8, ((uint8_t *)&t274));
                gm_b16_8 t276 = gm_l5;
                gm_f_perform_native_2ppar83((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t270, t271, t272, ((uint32_t)0ULL), t275, t276);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto me3;
mn267:;
ma15:;
            uint8_t t278 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t278 != ((uint8_t)45ULL)) goto mn277;
            {
                void * t280 = gm_l0;
                gm_tl874t2atnlh3 t281 = gm_l1;
                gm_t9qkh0rsooc8d t282;
                GM_ST(uint8_t, ((uint8_t *)&t282 + 0), ((uint8_t)2ULL));
                gm_b16_8 t283 = gm_l4;
                gm_b16_8 t284 = gm_l5;
                gm_f_perform_native_2ppar83((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t280, t281, t282, ((uint32_t)1ULL), t283, t284);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto me3;
mn277:;
ma16:;
            uint8_t t286 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t286 != ((uint8_t)46ULL)) goto mn285;
            {
                void * t288 = gm_l0;
                void * t289 = gm_l0;
                uint64_t t290 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t291 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t290 >= (uintptr_t)(t291)) gm_trap_index(t290, t291, gm_srcn3jjc71a2bv3, 196, 52);
                gm_b16_8 t292 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t290 * 16));
                gm_b16_8 t293 = t292;
                gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)&t293));
                unsigned __int128 t295 = gm_f_get_uint_bq8c4b7(t289, t294);
                uint8_t t296 = (uint8_t)t295;
                gm_f_env_exit_prbvpjt((void *)&(*(gm_tcfa6l7reoosn *)gm_out), t288, t296);
                goto gm_fdone;
            }
            goto me3;
mn285:;
ma17:;
            void * t298 = gm_l0;
            gm_tl874t2atnlh3 t299 = gm_l1;
            gm_b16_8 t300;
            GM_ST(void *, (uint8_t *)&t300, (void *)(gm_s27n8348bef64));
            GM_ST(uintptr_t, (uint8_t *)&t300 + 8, (uintptr_t)(62));
            gm_t76123gfet73g t301;
            gm_f_trap_qh84l9d((void *)&t301, (void *)NULL, cx, NULL, t298, t299, t300);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t302;
            GM_ST(uint8_t, (uint8_t *)&t302 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t302 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t302 + 4, 0);
            ((uint8_t *)&t302)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t302 + 8), t301);
            gm_tcfa6l7reoosn t303 = t302;
            uint8_t t304 = ((((uint8_t *)&t303))[0] != 0);
            F.ret = t302;
            goto gm_fret;
            goto me3;
mn297:;
ma18:;
            gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 167, 5);
me3:;
        }
        {
            void * t305 = gm_l0;
            gm_b16_8 t306 = gm_l5;
            int64_t t307 = gm_l6;
            gm_f_sys_ret_vpa2k8i(t305, t306, t307);
        }
    }
    gm_b0_1 t308;
    gm_tcfa6l7reoosn t309;
    GM_ST(uint8_t, (uint8_t *)&t309 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t309 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t309 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t309 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t309 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t309 + 24, 0);
    ((uint8_t *)&t309)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t309 + 8), t308);
    gm_tcfa6l7reoosn t310 = t309;
    F.ret = t310;
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
inline __attribute__((aligned(4))) void gm_f_sys_src_5svsr6i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_src_5svsr6i F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* b */
    gm_t2lqu8jsj511d gm_l3; /* sp */
    gm_b16_8 gm_l5; /* v */
    GM_STACK_CHECK(&F, 530u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        gm_t2lqu8jsj511d t4 = gm_f_slice_parts_1se5hu4(t2, t3);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 184);
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        gm_t934k1j7u70ct t9;
        gm_f_read_u6r4vns((void *)&t9, t6, t7, t8, ((uint64_t)1ULL));
        gm_t934k1j7u70ct t10 = t9;
        gm_b16_8 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
            t11 = t12;
        } else {
            gm_tpekhfs32op0f t13 = GM_LD(gm_tpekhfs32op0f, (((uint8_t *)&t10) + 8));
            F.l4 = t13;
            void * t14 = gm_l0;
            gm_tl874t2atnlh3 t15 = gm_l1;
            gm_t76123gfet73g t16;
            gm_f_mem_trap_ecogko0((void *)&t16, (void *)NULL, cx, NULL, t14, t15, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_td0ept12e7q8b t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t17 + 8), t16);
            gm_td0ept12e7q8b t18 = t17;
            uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
            F.ret = t17;
            goto gm_fret;
        }
        gm_l5 = t11;
    }
    {
        gm_b16_8 t20 = gm_l5;
        gm_b16_8 t21 = t20;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_td0ept12e7q8b t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t23 + 24, 0);
        ((uint8_t *)&t23)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 8), t22);
        gm_td0ept12e7q8b t24 = t23;
        uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
        F.ret = t23;
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
inline __attribute__((aligned(4))) void gm_f_sys_state_cvl1lvl(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_sys_state_cvl1lvl F;
    GM_STACK_CHECK(&F, 296u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t4c7ipjj9s3fp t2;
        gm_f_hash_map_3sndrtb((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t3;
        gm_f_hash_map_irh77t1((void *)&t3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t4;
        gm_f_hash_map_abi2bn7((void *)&t4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t5;
        gm_f_hash_map_ql49boo((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t6;
        gm_f_hash_map_3d5nsdr((void *)&t6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tf5srh4mp4b3i t7;
        memset(&t7, 0, sizeof t7);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 0, t2);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 56, t3);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 112, t4);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 168, t5);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 224, t6);
        GM_ST(int64_t, (uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 280, ((int64_t)0LL));
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 288, t7);
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
extern __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_sys_stdio_nsl9o9u(uint32_t a0);
__attribute__((aligned(4))) void gm_f_sys_strings_r85ragc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_strings_r85ragc F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* b */
    gm_t2lqu8jsj511d gm_l3; /* sp */
    uint64_t gm_l4; /* ps */
    gm_b16_8 gm_l6; /* items */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l11; /* s */
    GM_STACK_CHECK(&F, 1115u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        gm_t2lqu8jsj511d t4 = gm_f_slice_parts_1se5hu4(t2, t3);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 176));
        uint64_t t7 = gm_f_us_cuv01fp(t6);
        gm_l4 = t7;
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 184);
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint64_t t12 = gm_l4;
        uint64_t r13; bool o14 = __builtin_mul_overflow(((uint64_t)2ULL), t12, &r13);
        if (o14) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 143, 47);
        uint64_t t15 = r13;
        uint64_t t16 = (uint64_t)t15;
        uint64_t r17; bool o18 = __builtin_mul_overflow(t11, t16, &r17);
        uint64_t t19 = o18 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r17;
        gm_t934k1j7u70ct t20;
        gm_f_read_u6r4vns((void *)&t20, t9, t10, t19, ((uint64_t)1ULL));
        gm_t934k1j7u70ct t21 = t20;
        gm_b16_8 t22;
        if (!((((uint8_t *)&t21))[0] != 0)) {
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
            t22 = t23;
        } else {
            gm_tpekhfs32op0f t24 = GM_LD(gm_tpekhfs32op0f, (((uint8_t *)&t21) + 8));
            F.l5 = t24;
            void * t25 = gm_l0;
            gm_tl874t2atnlh3 t26 = gm_l1;
            gm_t76123gfet73g t27;
            gm_f_mem_trap_ecogko0((void *)&t27, (void *)NULL, cx, NULL, t25, t26, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l5)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9922qjstblh1 t28;
            GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t28 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t28 + 40, 0);
            ((uint8_t *)&t28)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t28 + 8), t27);
            gm_t9922qjstblh1 t29 = t28;
            uint8_t t30 = ((((uint8_t *)&t29))[0] != 0);
            F.ret = t28;
            goto gm_fret;
        }
        gm_l6 = t22;
    }
    {
        gm_f_list_epvducg((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t33 = gm_f_us_cuv01fp(t32);
            gm_l8 = t33;
        }
        {
            for (;;) {
                uint64_t t35 = gm_l9;
                uint64_t t36 = gm_l8;
                uint8_t t37 = (t35 < t36);
                if (!t37) goto lb34;
                {
                    uint64_t t40 = gm_l9;
                    uint64_t t41 = t40;
                    uint64_t t44 = GM_LD(uint64_t, ((uint8_t *)&t41));
                    gm_l10 = t44;
                    {
                        void * t46 = gm_l0;
                        gm_tl874t2atnlh3 t47 = gm_l1;
                        uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        void * t49 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                        uint64_t t50 = gm_l10;
                        uint64_t r51; bool o52 = __builtin_mul_overflow(t50, ((uint64_t)2ULL), &r51);
                        if (o52) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 40);
                        uint64_t t53 = r51;
                        uint64_t t54 = gm_l4;
                        uint64_t r55; bool o56 = __builtin_mul_overflow(t53, t54, &r55);
                        if (o56) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 40);
                        uint64_t t57 = r55;
                        uint64_t t58 = (uintptr_t)t57;
                        uint64_t t59 = gm_l10;
                        uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                        if (o61) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 146, 53);
                        uint64_t t62 = r60;
                        uint64_t r63; bool o64 = __builtin_mul_overflow(t62, ((uint64_t)2ULL), &r63);
                        if (o64) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 52);
                        uint64_t t65 = r63;
                        uint64_t t66 = gm_l4;
                        uint64_t r67; bool o68 = __builtin_mul_overflow(t65, t66, &r67);
                        if (o68) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 52);
                        uint64_t t69 = r67;
                        uint64_t t70 = (uintptr_t)t69;
                        if (t58 > t70 || t70 > (uintptr_t)t48) gm_trap_slice(t58, t70, t48, gm_srcn3jjc71a2bv3, 146, 34);
                        gm_b16_8 t71;
                        GM_ST(void *, (uint8_t *)&t71, (void *)(((uint8_t *)t49 + t58 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(t70 - t58));
                        gm_b16_8 t72 = t71;
                        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                        gm_td0ept12e7q8b t74;
                        gm_f_sys_src_5svsr6i((void *)&t74, (void *)NULL, cx, NULL, t46, t47, t73);
                        if (gm_unw.kind) {
                            goto gm_ll38;
                        }
                        gm_td0ept12e7q8b t75 = t74;
                        if (((((uint8_t *)&t75))[0] != 0)) {
                            gm_t76123gfet73g t76 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t75) + 8));
                            gm_t9922qjstblh1 t77;
                            GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t77 + 32, 0);
                            GM_ST(uint64_t, (uint8_t *)&t77 + 40, 0);
                            ((uint8_t *)&t77)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t77 + 8), t76);
                            F.ret = t77;
                            goto gm_fret;
                        }
                        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
                        gm_l11 = t78;
                    }
                    {
                        void * t79 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t80 = gm_l11;
                        gm_tftb1odc739t5 t81 = gm_f_push_3ef746b(t79, t80);
                        gm_tftb1odc739t5 t82 = t81;
                        if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_srcn3jjc71a2bv3, 147, 9);
                        gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
                    }
                    goto me42;
mn43:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 145, 5);
me42:;
                }
lc34:;
                {
                    uint64_t t85 = gm_l9;
                    uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                    uint64_t t88 = r86;
                    gm_l9 = t88;
                }
            }
gm_ll38:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb34; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc34; }
            }
            goto gm_lad;
lb34:;
        }
    }
    {
        gm_trqfvmjtt4sij t89 = F.l7;
        gm_t9922qjstblh1 t90;
        GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
        ((uint8_t *)&t90)[0] = 0;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t90 + 8), t89);
        gm_t9922qjstblh1 t91 = t90;
        uint8_t t92 = ((((uint8_t *)&t91))[0] != 0);
        F.ret = t90;
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
extern __attribute__((aligned(4))) int64_t gm_f_sys_unit_nrsqciq(gm_ttln0d0hd70en a0);
