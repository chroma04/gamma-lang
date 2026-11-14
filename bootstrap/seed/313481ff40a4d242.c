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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; } gm_tpcj7kul6hlib;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpcj7kul6hlib) == 296 && _Alignof(gm_tpcj7kul6hlib) == 8 && offsetof(gm_tpcj7kul6hlib, f0) == 0 && offsetof(gm_tpcj7kul6hlib, f1) == 40 && offsetof(gm_tpcj7kul6hlib, f2) == 80 && offsetof(gm_tpcj7kul6hlib, f3) == 136 && offsetof(gm_tpcj7kul6hlib, f4) == 176 && offsetof(gm_tpcj7kul6hlib, f5) == 216 && offsetof(gm_tpcj7kul6hlib, f6) == 256, "layout of `Types`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
struct gm_fr_adt_luat93d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_adt_name_h7mjik7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_apply_4fi86e0 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_arg_types_645ddbe {
    gm_frame h;
    gm_toa2vhe3l4hfr l4; /* a */
    gm_b0_1 ret;
};
struct gm_fr_array_th8sdi3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_cmp_arg_tej1vfv {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_cmp_bytes_31kmum5 {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_cmp_key_c6mirrv {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_cmp_ref_8ntrqll {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_cmp_u32_458kpku {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_cmp_usize_9u9h2fi {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_copied_hn10ogl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_copied_nbtatio {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_copied_u3c68o3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_effect_set_vtcpipr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_91ffn83 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f8s372n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fn_o0lgah5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_each_component_o5cuh7c {
    gm_frame h;
    gm_tddtjfggdat0d l18; /* sig */
    gm_tddtjfggdat0d l19; /* sig */
    gm_b0_1 ret;
};
struct gm_fr_fresh_adt_u9rnf9f {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_fresh_effect_2msfvhg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_fresh_param_442g4m9 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_4s138tg {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_intern_ia19mhl {
    gm_frame h;
    void * l0; /* t */
    gm_t53at80pfdus3 l1; /* k */
    uint8_t l3; /* flags */
    gm_t53at80pfdus3 l5; /* owned */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_signed_integer_s66djm7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_key_rank_icmtvso {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_key_u64_ljklb56 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_len_expr_i134fvg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_len_op_symbol_2qoiciu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ri1fn5p {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vmvl81b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_many_ptr_l4v11f8 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_mentions_error_37knu7r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_optional_rh6okj3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_owned_args_q41ihau {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_toa2vhe3l4hfr l4; /* a */
    gm_b16_8 ret;
};
struct gm_fr_owned_effects_cesofoh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* refs */
    gm_tnpifv20irnph ret;
};
struct gm_fr_owned_fn_ket04lo {
    gm_frame h;
    gm_tddtjfggdat0d l0; /* f */
    gm_tddtjfggdat0d ret;
};
struct gm_fr_owned_key_pirfo8n {
    gm_frame h;
    gm_t428ue2u3hf5g ret;
};
struct gm_fr_owned_kind_smkp36q {
    gm_frame h;
    gm_t53at80pfdus3 l0; /* k */
    gm_tddtjfggdat0d l3; /* f */
    gm_tddtjfggdat0d l4; /* f */
    gm_t53at80pfdus3 ret;
};
struct gm_fr_param_n8kgsdm {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_name_rbnop5l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_result_o9ras10 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_set_template_o09q96o {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_splice_tj8iedl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* args */
    gm_tiilmfd16v3fc ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_tuple_l3i6trt {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_type_ty_9ph2gbr {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_types_8a61ga3 {
    gm_frame h;
    gm_tpcj7kul6hlib l0; /* t */
    gm_tpcj7kul6hlib ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_key_rs1gk1r {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* bytes */
    gm_t428ue2u3hf5g ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_apply_4fi86e0(gm_t9qkh0rsooc8d a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_arg_types_645ddbe(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) int32_t gm_f_cmp_arg_tej1vfv(void * a0, void * a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_bytes_31kmum5(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_key_c6mirrv(void * a0, void * a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_u32_458kpku(uint32_t a0, uint32_t a1);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_usize_9u9h2fi(uint64_t a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_copied_hn10ogl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_copied_nbtatio(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_copied_u3c68o3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_91ffn83(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f8s372n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_lfqasgq(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o0lgah5(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_for_each_component_o5cuh7c(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_effect_2msfvhg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_param_442g4m9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_bkkldt5(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_4s138tg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_insert_6hqhcg6(void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint32_t gm_f_key_rank_icmtvso(void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_key_u64_ljklb56(const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_key_u64_ljklb56_c(gm_t428ue2u3hf5g a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t53at80pfdus3 gm_f_kind_jcnt4hm_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_len_expr_i134fvg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_len_op_symbol_2qoiciu(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ri1fn5p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_error_37knu7r(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_owned_args_q41ihau(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_owned_effects_cesofoh(void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_owned_fn_ket04lo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_owned_key_pirfo8n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_owned_kind_smkp36q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t1s1chs(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_nuvto78(void * a0, const void * a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_splice_tj8iedl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_tuple_l3i6trt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_types_8a61ga3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_usize_key_rs1gk1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s9hi985r6f61m[3];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_snenkhfev166s[3];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_adt_luat93d F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    gm_b16_8 gm_l2; /* args */
    GM_STACK_CHECK(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tiilmfd16v3fc t2;
    memset((uint8_t *)&t2 + 4, 0, 4);
    uint32_t t3 = gm_l1;
    GM_ST(uint32_t, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4 = gm_l2;
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 8, t4);
    gm_t53at80pfdus3 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)17ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t5 + 8), t2);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_name_h7mjik7 F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 136) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 268, 59);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 136) + 0)) + t3 * 16));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_pn2p277 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)4ULL));
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
inline __attribute__((aligned(4))) int32_t gm_f_cmp_u32_458kpku(uint32_t a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_u32_458kpku F;
    uint32_t gm_l0; /* a */
    uint32_t gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    int32_t t1;
    uint32_t t3 = gm_l0;
    uint32_t t4 = gm_l1;
    uint8_t t5 = (t3 < t4);
    if (!t5) goto ie2;
    int32_t t7 = ((int32_t)-1LL);
    t1 = t7;
    goto ix2;
ie2:;
    int32_t t8;
    uint32_t t10 = gm_l0;
    uint32_t t11 = gm_l1;
    uint8_t t12 = (t10 > t11);
    if (!t12) goto ie9;
    int32_t t14 = ((int32_t)1LL);
    t8 = t14;
    goto ix9;
ie9:;
    int32_t t16 = ((int32_t)0LL);
    t8 = t16;
ix9:;
    t1 = t8;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int32_t gm_f_cmp_usize_9u9h2fi(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_usize_9u9h2fi F;
    uint64_t gm_l0; /* a */
    uint64_t gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 78u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    int32_t t1;
    uint64_t t3 = gm_l0;
    uint64_t t4 = gm_l1;
    uint8_t t5 = (t3 < t4);
    if (!t5) goto ie2;
    int32_t t7 = ((int32_t)-1LL);
    t1 = t7;
    goto ix2;
ie2:;
    int32_t t8;
    uint64_t t10 = gm_l0;
    uint64_t t11 = gm_l1;
    uint8_t t12 = (t10 > t11);
    if (!t12) goto ie9;
    int32_t t14 = ((int32_t)1LL);
    t8 = t14;
    goto ix9;
ie9:;
    int32_t t16 = ((int32_t)0LL);
    t8 = t16;
ix9:;
    t1 = t8;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_copied_hn10ogl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_copied_hn10ogl F;
    gm_b16_8 gm_l0; /* xs */
    GM_STACK_CHECK(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_src0ih77f43mhf2, 209, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_copied_nbtatio(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_copied_nbtatio F;
    gm_b16_8 gm_l0; /* xs */
    GM_STACK_CHECK(&F, 125u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_vogsd72(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_src0ih77f43mhf2, 209, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_copied_u3c68o3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_copied_u3c68o3 F;
    gm_b16_8 gm_l0; /* xs */
    GM_STACK_CHECK(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_nof7vfj((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_lfqasgq(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_src0ih77f43mhf2, 209, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_name_prpp0ip F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 216) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 283, 62);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 216) + 0)) + t3 * 16));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t2 = t1;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    F.ret = t4;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_91ffn83(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_91ffn83 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 4)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f8s372n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f8s372n F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 4)));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_error_ki8i3k6 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)26ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o0lgah5(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_o0lgah5 F;
    gm_t88rcucg1b7uk gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_intern_ia19mhl *)((&F.h)->up))->l3));
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 0));
        uint64_t t4 = (uint64_t)t3;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_intern_ia19mhl *)((&F.h)->up))->l0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 40) + 0) + 8);
        if (t5 >= (uintptr_t)(t7)) gm_trap_index(t5, t7, gm_src0ih77f43mhf2, 161, 49);
        uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 40) + 0)) + t5 * 1));
        uint8_t t9 = (uint8_t)(t2 | t8);
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_intern_ia19mhl *)((&F.h)->up))->l3), t9);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_adt_u9rnf9f F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 136) + 0) + 8);
        uint32_t t4 = (uint32_t)t3;
        gm_l2 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 136);
        gm_b16_8 t7 = gm_l1;
        gm_tftb1odc739t5 t8 = gm_f_push_3ef746b(t6, t7);
        gm_tftb1odc739t5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_src0ih77f43mhf2, 263, 5);
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 176);
        uint32_t t13 = gm_l2;
        gm_tftb1odc739t5 t14 = gm_f_push_im9d4ro(t12, t13);
        gm_tftb1odc739t5 t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_src0ih77f43mhf2, 264, 5);
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
    }
    {
        uint32_t t17 = gm_l2;
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_effect_2msfvhg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_effect_2msfvhg F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 216);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_src0ih77f43mhf2, 279, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 216) + 0) + 8);
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_src0ih77f43mhf2, 280, 13);
        uint64_t t12 = r10;
        uint32_t t13 = (uint32_t)t12;
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_param_442g4m9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_param_442g4m9 F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 256);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_src0ih77f43mhf2, 286, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 256) + 0) + 8);
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_src0ih77f43mhf2, 287, 13);
        uint64_t t12 = r10;
        uint32_t t13 = (uint32_t)t12;
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_infer_3o82a0b F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 40) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 252, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)2ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 40) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)1ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_4s138tg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_4s138tg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_91ffn83();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    uint16_t t3 = gm_l2;
    gm_tjcj4j2gbh6c8 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(uint16_t, ((uint8_t *)&t4 + 2), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)26ULL)));
    F.ret = t5;
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
    GM_STACK_CHECK_SHALLOW(&F, 142u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t10 != ((uint8_t)0ULL)) goto pa9;
    goto po8;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t12 != ((uint8_t)1ULL)) goto pa11;
    goto po8;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t14 != ((uint8_t)2ULL)) goto pa13;
    goto po8;
pa13:;
    goto mn7;
po8:;
    t5 = ((uint8_t)1);
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn15:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
        gm_l2 = t8;
        {
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t14 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_key_rank_icmtvso(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_rank_icmtvso F;
    void * gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 33u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint32_t t2;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; default: goto ma8; }
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t11 != ((uint8_t)0ULL)) goto mn10;
    t2 = ((uint32_t)0ULL);
    goto me3;
mn10:;
ma5:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t13 != ((uint8_t)1ULL)) goto mn12;
    t2 = ((uint32_t)1ULL);
    goto me3;
mn12:;
ma6:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t15 != ((uint8_t)2ULL)) goto mn14;
    t2 = ((uint32_t)2ULL);
    goto me3;
mn14:;
ma7:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t17 != ((uint8_t)3ULL)) goto mn16;
    t2 = ((uint32_t)3ULL);
    goto me3;
mn16:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 449, 46);
me3:;
    F.ret = t2;
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
    GM_STACK_CHECK_SHALLOW(&F, 104u);
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
    gm_t53at80pfdus3 t6 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 56));
    F.ret = t6;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_084fcd6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
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
inline __attribute__((aligned(4))) void gm_f_list_ri1fn5p(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ri1fn5p F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_f8s372n();
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
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vmvl81b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fh7svmj();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_many_ptr_l4v11f8 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)9ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_error_37knu7r(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mentions_error_37knu7r F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 40) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 255, 62);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)4ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_never_ty_b949mgo F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)5ULL));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_optional_rh6okj3 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* inner */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t3 + 8), t2);
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t3);
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
inline __attribute__((aligned(4))) void gm_f_owned_fn_ket04lo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_owned_fn_ket04lo F;
    GM_STACK_CHECK(&F, 69u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 20, 0, 4);
    memset((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 41, 0, 7);
    gm_b16_8 t1 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 0));
    gm_b16_8 t2 = gm_f_copied_u3c68o3((void *)NULL, cx, NULL, t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a0) + 16));
    gm_tnpifv20irnph t4 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)a0) + 24));
    gm_tnpifv20irnph t5 = gm_f_owned_effects_cesofoh((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)a0) + 40));
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 0, t2);
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 16, t3);
    GM_ST(gm_tnpifv20irnph, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 24, t5);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 40, t6);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_param_n8kgsdm F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)19ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 8), t2);
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_name_rbnop5l F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 256) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 290, 61);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 256) + 0)) + t3 * 16));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_ptr_t1h2m2d F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)8ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_result_o9ras10 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* err */
    gm_t88rcucg1b7uk gm_l2; /* ok */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tgglga8ro3isc t4;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)14ULL));
    GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_set_template_o09q96o F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    uint32_t gm_l2; /* template */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 176) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_src0ih77f43mhf2, 272, 5);
        uint32_t t7 = gm_l2;
        GM_ST(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 176) + 0)) + t4 * 4), t7);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_slice_5dt9121 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)10ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t4 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t1, ((uint8_t)1), t3);
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
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_template_9o92cvv F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 176) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 176) + 0)) + t3 * 4));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_tuple_l3i6trt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_tuple_l3i6trt F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* elems */
    GM_STACK_CHECK(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t5 = t4;
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t6);
    gm_t53at80pfdus3 t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t7 + 8), t2);
    gm_t88rcucg1b7uk t8 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t8;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_type_ty_9ph2gbr F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)6ULL));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t1, ((uint8_t)0), ((uint16_t)8ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t2;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = t3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t5);
    gm_b16_8 t6 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t8);
    gm_t53at80pfdus3 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t9 + 8), t2);
    gm_t88rcucg1b7uk t10 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t10;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_apply_4fi86e0(gm_t9qkh0rsooc8d a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_apply_4fi86e0 F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    uint64_t gm_l1; /* a */
    uint64_t gm_l2; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 858u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; default: goto ma13; }
ma3:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t16 != ((uint8_t)0ULL)) goto mn15;
        uint64_t t17 = gm_l1;
        uint64_t t18 = gm_l2;
        uint64_t r19; bool o20 = __builtin_add_overflow(t17, t18, &r19);
        gm_tf5srh4mp4b3i t21;
        if (o20) {
        gm_tf5srh4mp4b3i t22;
        memset(&t22, 0, sizeof t22);
            t21 = t22;
        } else {
        uint64_t t23 = r19;
        gm_tf5srh4mp4b3i t24;
        GM_ST(uint8_t, (uint8_t *)&t24 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t24 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t24), t23);
        ((uint8_t *)&t24)[8] = 1;
            t21 = t24;
        }
        uint8_t t25 = 0;
        F.ret = t21;
        goto gm_fret;
        goto me2;
mn15:;
ma4:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t27 != ((uint8_t)1ULL)) goto mn26;
        uint64_t t28 = gm_l1;
        uint64_t t29 = gm_l2;
        uint64_t r30; bool o31 = __builtin_sub_overflow(t28, t29, &r30);
        gm_tf5srh4mp4b3i t32;
        if (o31) {
        gm_tf5srh4mp4b3i t33;
        memset(&t33, 0, sizeof t33);
            t32 = t33;
        } else {
        uint64_t t34 = r30;
        gm_tf5srh4mp4b3i t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t35), t34);
        ((uint8_t *)&t35)[8] = 1;
            t32 = t35;
        }
        uint8_t t36 = 0;
        F.ret = t32;
        goto gm_fret;
        goto me2;
mn26:;
ma5:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t38 != ((uint8_t)2ULL)) goto mn37;
        uint64_t t39 = gm_l1;
        uint64_t t40 = gm_l2;
        uint64_t r41; bool o42 = __builtin_mul_overflow(t39, t40, &r41);
        gm_tf5srh4mp4b3i t43;
        if (o42) {
        gm_tf5srh4mp4b3i t44;
        memset(&t44, 0, sizeof t44);
            t43 = t44;
        } else {
        uint64_t t45 = r41;
        gm_tf5srh4mp4b3i t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t46 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t46), t45);
        ((uint8_t *)&t46)[8] = 1;
            t43 = t46;
        }
        uint8_t t47 = 0;
        F.ret = t43;
        goto gm_fret;
        goto me2;
mn37:;
ma6:;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t49 != ((uint8_t)3ULL)) goto mn48;
        gm_tf5srh4mp4b3i t50;
        uint64_t t52 = gm_l2;
        uint8_t t53 = ((t52 == ((uint64_t)0ULL)));
        if (!t53) goto ie51;
        gm_tf5srh4mp4b3i t55;
        memset(&t55, 0, sizeof t55);
        gm_tf5srh4mp4b3i t56 = t55;
        t50 = t56;
        goto ix51;
ie51:;
        uint64_t t58 = gm_l1;
        uint64_t t59 = gm_l2;
        if (t59 == 0) gm_trap("division by zero", gm_src0ih77f43mhf2, 342, 50);
        uint64_t t60 = (uint64_t)(t58 / t59);
        uint64_t t61 = t60;
        gm_tf5srh4mp4b3i t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t62 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t62), t61);
        ((uint8_t *)&t62)[8] = 1;
        t50 = t62;
ix51:;
        uint8_t t63 = 0;
        F.ret = t50;
        goto gm_fret;
        goto me2;
mn48:;
ma7:;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t65 != ((uint8_t)4ULL)) goto mn64;
        gm_tf5srh4mp4b3i t66;
        uint64_t t68 = gm_l2;
        uint8_t t69 = ((t68 == ((uint64_t)0ULL)));
        if (!t69) goto ie67;
        gm_tf5srh4mp4b3i t71;
        memset(&t71, 0, sizeof t71);
        gm_tf5srh4mp4b3i t72 = t71;
        t66 = t72;
        goto ix67;
ie67:;
        uint64_t t74 = gm_l1;
        uint64_t t75 = gm_l2;
        if (t75 == 0) gm_trap("division by zero", gm_src0ih77f43mhf2, 343, 50);
        uint64_t t76 = (uint64_t)(t74 % t75);
        uint64_t t77 = t76;
        gm_tf5srh4mp4b3i t78;
        GM_ST(uint8_t, (uint8_t *)&t78 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t78 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t78 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t78), t77);
        ((uint8_t *)&t78)[8] = 1;
        t66 = t78;
ix67:;
        uint8_t t79 = 0;
        F.ret = t66;
        goto gm_fret;
        goto me2;
mn64:;
ma8:;
        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t81 != ((uint8_t)5ULL)) goto mn80;
        gm_tf5srh4mp4b3i t82;
        uint64_t t84 = gm_l2;
        uint8_t t85 = (t84 >= ((uint64_t)64ULL));
        if (!t85) goto ie83;
        gm_tf5srh4mp4b3i t87;
        memset(&t87, 0, sizeof t87);
        gm_tf5srh4mp4b3i t88 = t87;
        t82 = t88;
        goto ix83;
ie83:;
        uint64_t t90 = gm_l1;
        uint64_t t91 = gm_l2;
        unsigned __int128 n92 = (unsigned __int128)t91;
        if (n92 >= 64) gm_trap_shift((uint64_t)n92, "u64", gm_src0ih77f43mhf2, 344, 51);
        uint64_t r93 = (uint64_t)((uint64_t)t90 << (int)n92);
        bool o94 = (uint64_t)(r93 >> (int)n92) != t90;
        gm_tf5srh4mp4b3i t95;
        if (o94) {
        gm_tf5srh4mp4b3i t96;
        memset(&t96, 0, sizeof t96);
            t95 = t96;
        } else {
        uint64_t t97 = r93;
        gm_tf5srh4mp4b3i t98;
        GM_ST(uint8_t, (uint8_t *)&t98 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t98 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t98 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t98), t97);
        ((uint8_t *)&t98)[8] = 1;
            t95 = t98;
        }
        gm_tf5srh4mp4b3i t99 = t95;
        t82 = t99;
ix83:;
        uint8_t t100 = 0;
        F.ret = t82;
        goto gm_fret;
        goto me2;
mn80:;
ma9:;
        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t102 != ((uint8_t)6ULL)) goto mn101;
        gm_tf5srh4mp4b3i t103;
        uint64_t t105 = gm_l2;
        uint8_t t106 = (t105 >= ((uint64_t)64ULL));
        if (!t106) goto ie104;
        uint64_t t108 = ((uint64_t)0ULL);
        gm_tf5srh4mp4b3i t109;
        GM_ST(uint8_t, (uint8_t *)&t109 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t109 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t109 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t109), t108);
        ((uint8_t *)&t109)[8] = 1;
        t103 = t109;
        goto ix104;
ie104:;
        uint64_t t111 = gm_l1;
        uint64_t t112 = gm_l2;
        unsigned __int128 n113 = (unsigned __int128)t112;
        if (n113 >= 64) gm_trap_shift((uint64_t)n113, "u64", gm_src0ih77f43mhf2, 345, 48);
        uint64_t t114 = (uint64_t)(t111 >> (int)n113);
        uint64_t t115 = t114;
        gm_tf5srh4mp4b3i t116;
        GM_ST(uint8_t, (uint8_t *)&t116 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t116 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t116 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t116), t115);
        ((uint8_t *)&t116)[8] = 1;
        t103 = t116;
ix104:;
        uint8_t t117 = 0;
        F.ret = t103;
        goto gm_fret;
        goto me2;
mn101:;
ma10:;
        uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t119 != ((uint8_t)7ULL)) goto mn118;
        uint64_t t120 = gm_l1;
        uint64_t t121 = gm_l2;
        uint64_t t122 = (uint64_t)(t120 & t121);
        gm_tf5srh4mp4b3i t123;
        GM_ST(uint8_t, (uint8_t *)&t123 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t123 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t123 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t123), t122);
        ((uint8_t *)&t123)[8] = 1;
        uint8_t t124 = 0;
        F.ret = t123;
        goto gm_fret;
        goto me2;
mn118:;
ma11:;
        uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t126 != ((uint8_t)8ULL)) goto mn125;
        uint64_t t127 = gm_l1;
        uint64_t t128 = gm_l2;
        uint64_t t129 = (uint64_t)(t127 | t128);
        gm_tf5srh4mp4b3i t130;
        GM_ST(uint8_t, (uint8_t *)&t130 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t130 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t130 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t130), t129);
        ((uint8_t *)&t130)[8] = 1;
        uint8_t t131 = 0;
        F.ret = t130;
        goto gm_fret;
        goto me2;
mn125:;
ma12:;
        uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t133 != ((uint8_t)9ULL)) goto mn132;
        uint64_t t134 = gm_l1;
        uint64_t t135 = gm_l2;
        uint64_t t136 = (uint64_t)(t134 ^ t135);
        gm_tf5srh4mp4b3i t137;
        GM_ST(uint8_t, (uint8_t *)&t137 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t137 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t137 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t137), t136);
        ((uint8_t *)&t137)[8] = 1;
        uint8_t t138 = 0;
        F.ret = t137;
        goto gm_fret;
        goto me2;
mn132:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 338, 5);
me2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_arg_types_645ddbe(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_arg_types_645ddbe F;
    gm_b16_8 gm_l0; /* args */
    gm_closure gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t88rcucg1b7uk gm_l5; /* t */
    GM_STACK_CHECK(&F, 246u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_toa2vhe3l4hfr t15 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 40));
                    F.l4 = t15;
                    {
                        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        if (t18 != ((uint8_t)0ULL)) goto ie17;
                        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
                        gm_l5 = t19;
                        {
                            gm_closure t21 = gm_l1;
                            gm_t88rcucg1b7uk t22 = gm_l5;
                            gm_closure t23 = t21;
                            gm_node *ccx24 = t23.cx;
                            void * t25 = t23.code;
                            gm_b0_1 t26;
                            if (GM_PTR_TAGGED(t25)) {
                                ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t25))(GM_CONV(gm_csguankko4lmhd, t22));
                            } else {
                                t26 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t25)(t23.env, ccx24, NULL, t22);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 243, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t28 = gm_l3;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l3 = t31;
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_array_th8sdi3 F;
    void * gm_l0; /* t */
    uint64_t gm_l1; /* len */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t8nckeqfenl8a t2;
    memset((uint8_t *)&t2 + 20, 0, 4);
    uint64_t t3 = gm_l1;
    GM_ST(uint64_t, (uint8_t *)&t2 + 0, t3);
    gm_t88rcucg1b7uk t4 = gm_l2;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t2 + 8, t4);
    gm_tnnjeerolos0a t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tnnjeerolos0a, (uint8_t *)&t2 + 12, t5);
    gm_t53at80pfdus3 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)11ULL));
    GM_ST(gm_t8nckeqfenl8a, ((uint8_t *)&t6 + 8), t2);
    gm_t88rcucg1b7uk t7 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_arg_tej1vfv(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_arg_tej1vfv F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    gm_t88rcucg1b7uk gm_l2; /* x */
    gm_t88rcucg1b7uk gm_l3; /* y */
    gm_t428ue2u3hf5g gm_l4; /* x */
    gm_t428ue2u3hf5g gm_l5; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 232u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t5 != ((uint8_t)0ULL)) goto mn4;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 8));
        gm_l2 = t6;
        {
            void * t9 = gm_l1;
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t9) + 0));
            if (t10 != ((uint8_t)0ULL)) goto ie8;
            gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 8));
            gm_l3 = t11;
            {
                uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
                uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                int32_t t15 = gm_f_cmp_u32_458kpku(t13, t14);
                uint8_t t16 = 0;
                F.ret = t15;
                goto gm_fret;
            }
            goto ix8;
ie8:;
            {
                uint8_t t18 = 0;
                F.ret = ((int32_t)-1LL);
                goto gm_fret;
            }
ix8:;
        }
        goto me3;
mn4:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t20 != ((uint8_t)1ULL)) goto mn19;
        gm_t428ue2u3hf5g t21 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)t2) + 8));
        gm_l4 = t21;
        {
            void * t24 = gm_l1;
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)t24) + 0));
            if (t25 != ((uint8_t)1ULL)) goto ie23;
            gm_t428ue2u3hf5g t26 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)t24) + 8));
            gm_l5 = t26;
            {
                void * t28 = (void *)((uint8_t *)&gm_l4);
                void * t29 = (void *)((uint8_t *)&gm_l5);
                int32_t t30 = gm_f_cmp_key_c6mirrv(t28, t29);
                uint8_t t31 = 0;
                F.ret = t30;
                goto gm_fret;
            }
            goto ix23;
ie23:;
            {
                uint8_t t33 = 0;
                F.ret = ((int32_t)1LL);
                goto gm_fret;
            }
ix23:;
        }
        goto me3;
mn19:;
        gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 415, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int32_t gm_f_cmp_bytes_31kmum5(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_bytes_31kmum5 F;
    gm_b16_8 gm_l0; /* x */
    gm_b16_8 gm_l1; /* y */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 281u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t t5 = (t3 < t4 ? t3 : t4);
            gm_l2 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l3;
                uint64_t t8 = gm_l2;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l3;
                    uint64_t t13 = t12;
                    uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&t13));
                    gm_l4 = t16;
                    {
                        uint64_t t19 = gm_l4;
                        uint64_t t20 = (uintptr_t)t19;
                        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_src0ih77f43mhf2, 458, 12);
                        uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t20 * 1));
                        uint64_t t23 = gm_l4;
                        uint64_t t24 = (uintptr_t)t23;
                        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_src0ih77f43mhf2, 458, 24);
                        uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t24 * 1));
                        uint8_t t27 = (!(t22 == t26));
                        if (!t27) goto ie18;
                        {
                            int32_t t29;
                            uint64_t t31 = gm_l4;
                            uint64_t t32 = (uintptr_t)t31;
                            uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_src0ih77f43mhf2, 458, 41);
                            uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t32 * 1));
                            uint64_t t35 = gm_l4;
                            uint64_t t36 = (uintptr_t)t35;
                            uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t36 >= (uintptr_t)(t37)) gm_trap_index(t36, t37, gm_src0ih77f43mhf2, 458, 48);
                            uint8_t t38 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t36 * 1));
                            uint8_t t39 = (t34 < t38);
                            if (!t39) goto ie30;
                            int32_t t41 = ((int32_t)-1LL);
                            t29 = t41;
                            goto ix30;
ie30:;
                            int32_t t43 = ((int32_t)1LL);
                            t29 = t43;
ix30:;
                            uint8_t t44 = 0;
                            F.ret = t29;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 457, 5);
me14:;
                }
lc6:;
                {
                    uint64_t t46 = gm_l3;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l3 = t49;
                }
            }
lb6:;
        }
    }
    {
        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        int32_t t52 = gm_f_cmp_usize_9u9h2fi(t50, t51);
        uint8_t t53 = 0;
        F.ret = t52;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) int32_t gm_f_cmp_key_c6mirrv(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_key_c6mirrv F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    uint32_t gm_l2; /* ia */
    uint32_t gm_l3; /* ib */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_b16_8 gm_l5; /* x */
    gm_t88rcucg1b7uk gm_l6; /* u */
    gm_b16_8 gm_l7; /* y */
    uint32_t gm_l8; /* i */
    uint32_t gm_l9; /* j */
    gm_b16_8 gm_l10; /* x */
    gm_b16_8 gm_l11; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 391u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_key_rank_icmtvso(t2);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l1;
        uint32_t t5 = gm_f_key_rank_icmtvso(t4);
        gm_l3 = t5;
    }
    {
        uint32_t t7 = gm_l2;
        uint32_t t8 = gm_l3;
        uint8_t t9 = (!(t7 == t8));
        if (!t9) goto ie6;
        {
            uint32_t t11 = gm_l2;
            uint32_t t12 = gm_l3;
            int32_t t13 = gm_f_cmp_u32_458kpku(t11, t12);
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t15 = gm_l0;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)t15) + 0));
        switch (t22) { case ((uint8_t)0ULL): goto ma17; case ((uint8_t)1ULL): goto ma18; case ((uint8_t)2ULL): goto ma19; case ((uint8_t)3ULL): goto ma20; default: goto ma21; }
ma17:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t15) + 0));
        if (t24 != ((uint8_t)0ULL)) goto mn23;
        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t15) + 8) + 0));
        gm_l4 = t25;
        gm_b16_8 t26 = GM_LD(gm_b16_8, ((((uint8_t *)t15) + 8) + 8));
        gm_l5 = t26;
        {
            void * t29 = gm_l1;
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
            if (t30 != ((uint8_t)0ULL)) goto ie28;
            gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t29) + 8) + 0));
            gm_l6 = t31;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((((uint8_t *)t29) + 8) + 8));
            gm_l7 = t32;
            {
                uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
                uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                uint8_t t37 = (!(t35 == t36));
                if (!t37) goto ie34;
                {
                    uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
                    uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                    int32_t t41 = gm_f_cmp_u32_458kpku(t39, t40);
                    uint8_t t42 = 0;
                    F.ret = t41;
                    goto gm_fret;
                }
                goto ix34;
ie34:;
ix34:;
            }
            {
                gm_b16_8 t43 = gm_l5;
                gm_b16_8 t44 = gm_l7;
                int32_t t45 = gm_f_cmp_bytes_31kmum5(t43, t44);
                uint8_t t46 = 0;
                F.ret = t45;
                goto gm_fret;
            }
            goto ix28;
ie28:;
            {
                uint8_t t48 = 0;
                F.ret = ((int32_t)0LL);
                goto gm_fret;
            }
ix28:;
        }
        goto me16;
mn23:;
ma18:;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)t15) + 0));
        if (t50 != ((uint8_t)1ULL)) goto mn49;
        uint32_t t51 = GM_LD(uint32_t, ((((uint8_t *)t15) + 8) + 0));
        gm_l8 = t51;
        {
            void * t54 = gm_l1;
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)t54) + 0));
            if (t55 != ((uint8_t)1ULL)) goto ie53;
            uint32_t t56 = GM_LD(uint32_t, ((((uint8_t *)t54) + 8) + 0));
            gm_l9 = t56;
            {
                uint32_t t58 = gm_l8;
                uint32_t t59 = gm_l9;
                int32_t t60 = gm_f_cmp_u32_458kpku(t58, t59);
                uint8_t t61 = 0;
                F.ret = t60;
                goto gm_fret;
            }
            goto ix53;
ie53:;
            {
                uint8_t t63 = 0;
                F.ret = ((int32_t)0LL);
                goto gm_fret;
            }
ix53:;
        }
        goto me16;
mn49:;
ma19:;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)t15) + 0));
        if (t65 != ((uint8_t)2ULL)) goto mn64;
        uint8_t t66 = 0;
        F.ret = ((int32_t)0LL);
        goto gm_fret;
        goto me16;
mn64:;
ma20:;
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)t15) + 0));
        if (t68 != ((uint8_t)3ULL)) goto mn67;
        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 8));
        gm_l10 = t69;
        {
            void * t72 = gm_l1;
            uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)t72) + 0));
            if (t73 != ((uint8_t)3ULL)) goto ie71;
            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)t72) + 8));
            gm_l11 = t74;
            {
                gm_b16_8 t76 = gm_l10;
                gm_b16_8 t77 = gm_l11;
                int32_t t78 = gm_f_cmp_bytes_31kmum5(t76, t77);
                uint8_t t79 = 0;
                F.ret = t78;
                goto gm_fret;
            }
            goto ix71;
ie71:;
            {
                uint8_t t81 = 0;
                F.ret = ((int32_t)0LL);
                goto gm_fret;
            }
ix71:;
        }
        goto me16;
mn67:;
ma21:;
        gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 431, 5);
me16:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_ref_8ntrqll F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    int32_t gm_l6; /* c */
    GM_STACK_CHECK(&F, 359u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 0));
        void * t5 = gm_l1;
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t5) + 0));
        uint8_t t7 = (!(t4 == t6));
        if (!t7) goto ie2;
        {
            int32_t t9;
            void * t11 = gm_l0;
            uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)t11) + 0));
            void * t13 = gm_l1;
            uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t13) + 0));
            uint8_t t15 = (t12 < t14);
            if (!t15) goto ie10;
            int32_t t17 = ((int32_t)-1LL);
            t9 = t17;
            goto ix10;
ie10:;
            int32_t t19 = ((int32_t)1LL);
            t9 = t19;
ix10:;
            uint8_t t20 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t21 = gm_l0;
        uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)t21) + 8) + 8);
        void * t23 = gm_l1;
        uint64_t t24 = GM_LD(uintptr_t, (((uint8_t *)t23) + 8) + 8);
        uint64_t t25 = (t22 < t24 ? t22 : t24);
        gm_l2 = t25;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t27 = gm_l2;
            gm_l3 = t27;
        }
        {
            for (;;) {
                uint64_t t29 = gm_l4;
                uint64_t t30 = gm_l3;
                uint8_t t31 = (t29 < t30);
                if (!t31) goto lb28;
                {
                    uint64_t t34 = gm_l4;
                    uint64_t t35 = t34;
                    uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&t35));
                    gm_l5 = t38;
                    {
                        uint64_t t40 = gm_l5;
                        uint64_t t41 = (uintptr_t)t40;
                        void * t42 = gm_l0;
                        uint64_t t43 = GM_LD(uintptr_t, (((uint8_t *)t42) + 8) + 8);
                        if (t41 >= (uintptr_t)(t43)) gm_trap_index(t41, t43, gm_src0ih77f43mhf2, 408, 23);
                        void * t44 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t42) + 8)) + t41 * 40);
                        uint64_t t45 = gm_l5;
                        uint64_t t46 = (uintptr_t)t45;
                        void * t47 = gm_l1;
                        uint64_t t48 = GM_LD(uintptr_t, (((uint8_t *)t47) + 8) + 8);
                        if (t46 >= (uintptr_t)(t48)) gm_trap_index(t46, t48, gm_src0ih77f43mhf2, 408, 35);
                        void * t49 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t47) + 8)) + t46 * 40);
                        int32_t t50 = gm_f_cmp_arg_tej1vfv(t44, t49);
                        gm_l6 = t50;
                    }
                    {
                        int32_t t52 = gm_l6;
                        uint8_t t53 = (!(t52 == ((int32_t)0LL)));
                        if (!t53) goto ie51;
                        {
                            int32_t t55 = gm_l6;
                            uint8_t t56 = 0;
                            F.ret = t55;
                            goto gm_fret;
                        }
                        goto ix51;
ie51:;
ix51:;
                    }
                    goto me36;
mn37:;
                    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 407, 5);
me36:;
                }
lc28:;
                {
                    uint64_t t58 = gm_l4;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l4 = t61;
                }
            }
lb28:;
        }
    }
    {
        void * t62 = gm_l0;
        uint64_t t63 = GM_LD(uintptr_t, (((uint8_t *)t62) + 8) + 8);
        void * t64 = gm_l1;
        uint64_t t65 = GM_LD(uintptr_t, (((uint8_t *)t64) + 8) + 8);
        int32_t t66 = gm_f_cmp_usize_9u9h2fi(t63, t65);
        uint8_t t67 = 0;
        F.ret = t66;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) int32_t gm_f_cmp_u32_458kpku(uint32_t a0, uint32_t a1);
extern __attribute__((aligned(4))) int32_t gm_f_cmp_usize_9u9h2fi(uint64_t a0, uint64_t a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_effect_set_vtcpipr F;
    gm_b16_8 gm_l0; /* effects */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tiilmfd16v3fc gm_l4; /* e */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 434u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_084fcd6((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tiilmfd16v3fc t15 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 24));
                    gm_l4 = t15;
                    {
                        uint64_t t17 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
                        gm_l5 = t17;
                    }
                    {
                        for (;;) {
                            uint64_t t19 = gm_l5;
                            uint8_t t20 = (t19 > ((uint64_t)0ULL));
                            if (!t20) goto lb18;
                            uint64_t t21 = gm_l5;
                            uint64_t r22; bool o23 = __builtin_sub_overflow(t21, ((uint64_t)1ULL), &r22);
                            if (o23) gm_trap("integer overflow in `-`", gm_src0ih77f43mhf2, 394, 44);
                            uint64_t t24 = r22;
                            uint64_t t25 = (uintptr_t)t24;
                            uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
                            if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_src0ih77f43mhf2, 394, 34);
                            void * t27 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l1) + 0)) + t25 * 24);
                            void * t28 = (void *)((uint8_t *)&gm_l4);
                            int32_t t29 = gm_f_cmp_ref_8ntrqll(t27, t28);
                            uint8_t t30 = (t29 > ((int32_t)0LL));
                            if (!t30) goto lb18;
                            {
                                uint64_t t33 = gm_l5;
                                uint64_t r34; bool o35 = __builtin_sub_overflow(t33, ((uint64_t)1ULL), &r34);
                                if (o35) gm_trap("integer overflow in `-`", gm_src0ih77f43mhf2, 394, 62);
                                uint64_t t36 = r34;
                                gm_l5 = t36;
                            }
lc18:;
                        }
lb18:;
                    }
                    {
                        uint64_t t38 = gm_l5;
                        uint8_t t39 = (t38 > ((uint64_t)0ULL));
                        if (!t39) goto ie37;
                        uint64_t t40 = gm_l5;
                        uint64_t r41; bool o42 = __builtin_sub_overflow(t40, ((uint64_t)1ULL), &r41);
                        if (o42) gm_trap("integer overflow in `-`", gm_src0ih77f43mhf2, 395, 41);
                        uint64_t t43 = r41;
                        uint64_t t44 = (uintptr_t)t43;
                        uint64_t t45 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
                        if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_src0ih77f43mhf2, 395, 31);
                        void * t46 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l1) + 0)) + t44 * 24);
                        void * t47 = (void *)((uint8_t *)&gm_l4);
                        int32_t t48 = gm_f_cmp_ref_8ntrqll(t46, t47);
                        uint8_t t49 = ((t48 == ((int32_t)0LL)));
                        if (!t49) goto ie37;
                        {
                            goto lc4;
                        }
                        goto ix37;
ie37:;
ix37:;
                    }
                    {
                        void * t51 = (void *)((uint8_t *)&F.l1);
                        uint64_t t52 = gm_l5;
                        gm_tftb1odc739t5 t53 = gm_f_insert_6hqhcg6(t51, t52, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l4)));
                        gm_tftb1odc739t5 t54 = t53;
                        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_src0ih77f43mhf2, 396, 9);
                        gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 391, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t57 = gm_l3;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l3 = t60;
                }
            }
lb4:;
        }
    }
    {
        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t62 = t61;
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
        gm_tnpifv20irnph t64;
        GM_ST(gm_b16_8, ((uint8_t *)&t64 + 0), t63);
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
extern __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o0lgah5(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_for_each_component_o5cuh7c(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_for_each_component_o5cuh7c F;
    void * gm_l0; /* k */
    gm_closure gm_l1; /* f */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_t88rcucg1b7uk gm_l7; /* t */
    gm_t88rcucg1b7uk gm_l8; /* a */
    gm_t88rcucg1b7uk gm_l9; /* b */
    gm_t88rcucg1b7uk gm_l10; /* a */
    gm_t88rcucg1b7uk gm_l11; /* b */
    gm_t88rcucg1b7uk gm_l12; /* a */
    gm_t88rcucg1b7uk gm_l13; /* b */
    gm_b16_8 gm_l14; /* elems */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_t88rcucg1b7uk gm_l17; /* e */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_t88rcucg1b7uk gm_l22; /* p */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_tiilmfd16v3fc gm_l25; /* e */
    gm_b16_8 gm_l26; /* args */
    gm_tnpifv20irnph gm_l27; /* set */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_tiilmfd16v3fc gm_l30; /* e */
    GM_STACK_CHECK(&F, 1623u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma4; case ((uint8_t)9ULL): goto ma4; case ((uint8_t)10ULL): goto ma4; case ((uint8_t)11ULL): goto ma5; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma4; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma4; case ((uint8_t)19ULL): goto ma13; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma13; default: goto ma13; }
ma4:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t19 != ((uint8_t)8ULL)) goto pa18;
        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t2) + 8) + 4));
        gm_l2 = t20;
        goto po17;
pa18:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t22 != ((uint8_t)9ULL)) goto pa21;
        gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t2) + 8) + 4));
        gm_l2 = t23;
        goto po17;
pa21:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t25 != ((uint8_t)10ULL)) goto pa24;
        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t2) + 8) + 4));
        gm_l2 = t26;
        goto po17;
pa24:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t28 != ((uint8_t)13ULL)) goto pa27;
        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 8));
        gm_l2 = t29;
        goto po17;
pa27:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t31 != ((uint8_t)18ULL)) goto pa30;
        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 8));
        gm_l2 = t32;
        goto po17;
pa30:;
        goto mn16;
po17:;
        gm_closure t33 = gm_l1;
        gm_t88rcucg1b7uk t34 = gm_l2;
        gm_closure t35 = t33;
        gm_node *ccx36 = t35.cx;
        void * t37 = t35.code;
        gm_b0_1 t38;
        if (GM_PTR_TAGGED(t37)) {
            ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t37))(GM_CONV(gm_csguankko4lmhd, t34));
        } else {
            t38 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t37)(t35.env, ccx36, NULL, t34);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn16:;
ma5:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t40 != ((uint8_t)11ULL)) goto mn39;
        gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t41;
        gm_closure t42 = gm_l1;
        gm_t88rcucg1b7uk t43 = gm_l7;
        gm_closure t44 = t42;
        gm_node *ccx45 = t44.cx;
        void * t46 = t44.code;
        gm_b0_1 t47;
        if (GM_PTR_TAGGED(t46)) {
            ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t46))(GM_CONV(gm_csguankko4lmhd, t43));
        } else {
            t47 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t46)(t44.env, ccx45, NULL, t43);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn39:;
ma6:;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t49 != ((uint8_t)20ULL)) goto mn48;
        gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t2) + 8) + 0));
        gm_l8 = t50;
        gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t2) + 8) + 4));
        gm_l9 = t51;
        {
            gm_closure t53 = gm_l1;
            gm_t88rcucg1b7uk t54 = gm_l8;
            gm_closure t55 = t53;
            gm_node *ccx56 = t55.cx;
            void * t57 = t55.code;
            gm_b0_1 t58;
            if (GM_PTR_TAGGED(t57)) {
                ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t57))(GM_CONV(gm_csguankko4lmhd, t54));
            } else {
                t58 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t57)(t55.env, ccx56, NULL, t54);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t59 = gm_l1;
            gm_t88rcucg1b7uk t60 = gm_l9;
            gm_closure t61 = t59;
            gm_node *ccx62 = t61.cx;
            void * t63 = t61.code;
            gm_b0_1 t64;
            if (GM_PTR_TAGGED(t63)) {
                ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t63))(GM_CONV(gm_csguankko4lmhd, t60));
            } else {
                t64 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t63)(t61.env, ccx62, NULL, t60);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn48:;
ma7:;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t66 != ((uint8_t)22ULL)) goto mn65;
        gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t2) + 8) + 4));
        gm_l10 = t67;
        gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t2) + 8) + 8));
        gm_l11 = t68;
        {
            gm_closure t70 = gm_l1;
            gm_t88rcucg1b7uk t71 = gm_l10;
            gm_closure t72 = t70;
            gm_node *ccx73 = t72.cx;
            void * t74 = t72.code;
            gm_b0_1 t75;
            if (GM_PTR_TAGGED(t74)) {
                ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t74))(GM_CONV(gm_csguankko4lmhd, t71));
            } else {
                t75 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t74)(t72.env, ccx73, NULL, t71);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t76 = gm_l1;
            gm_t88rcucg1b7uk t77 = gm_l11;
            gm_closure t78 = t76;
            gm_node *ccx79 = t78.cx;
            void * t80 = t78.code;
            gm_b0_1 t81;
            if (GM_PTR_TAGGED(t80)) {
                ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t80))(GM_CONV(gm_csguankko4lmhd, t77));
            } else {
                t81 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t80)(t78.env, ccx79, NULL, t77);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn65:;
ma8:;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t83 != ((uint8_t)14ULL)) goto mn82;
        gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t2) + 8) + 0));
        gm_l12 = t84;
        gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t2) + 8) + 4));
        gm_l13 = t85;
        {
            gm_closure t87 = gm_l1;
            gm_t88rcucg1b7uk t88 = gm_l12;
            gm_closure t89 = t87;
            gm_node *ccx90 = t89.cx;
            void * t91 = t89.code;
            gm_b0_1 t92;
            if (GM_PTR_TAGGED(t91)) {
                ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t91))(GM_CONV(gm_csguankko4lmhd, t88));
            } else {
                t92 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t91)(t89.env, ccx90, NULL, t88);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t93 = gm_l1;
            gm_t88rcucg1b7uk t94 = gm_l13;
            gm_closure t95 = t93;
            gm_node *ccx96 = t95.cx;
            void * t97 = t95.code;
            gm_b0_1 t98;
            if (GM_PTR_TAGGED(t97)) {
                ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t97))(GM_CONV(gm_csguankko4lmhd, t94));
            } else {
                t98 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t97)(t95.env, ccx96, NULL, t94);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn82:;
ma9:;
        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t100 != ((uint8_t)12ULL)) goto mn99;
        gm_b16_8 t101 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 0));
        gm_l14 = t101;
        {
            gm_b16_8 t103 = gm_l14;
            gm_l15 = t103;
        }
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t105 = gm_l16;
                uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t107 = (t105 < t106);
                if (!t107) goto lb104;
                {
                    uint64_t t110 = gm_l16;
                    uint64_t t111 = (uintptr_t)t110;
                    uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_t88rcucg1b7uk t115 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t111 * 4));
                    gm_l17 = t115;
                    {
                        gm_closure t117 = gm_l1;
                        gm_t88rcucg1b7uk t118 = gm_l17;
                        gm_closure t119 = t117;
                        gm_node *ccx120 = t119.cx;
                        void * t121 = t119.code;
                        gm_b0_1 t122;
                        if (GM_PTR_TAGGED(t121)) {
                            ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t121))(GM_CONV(gm_csguankko4lmhd, t118));
                        } else {
                            t122 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t121)(t119.env, ccx120, NULL, t118);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll108;
                        }
                    }
                    goto me113;
mn114:;
                    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 230, 29);
me113:;
                }
lc104:;
                {
                    uint64_t t124 = gm_l16;
                    uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                    uint64_t t127 = r125;
                    gm_l16 = t127;
                }
            }
gm_ll108:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb104; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc104; }
            }
            goto gm_lad;
lb104:;
        }
        goto me3;
mn99:;
ma10:;
        uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t131 != ((uint8_t)15ULL)) goto pa130;
        gm_tddtjfggdat0d t132 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)t2) + 8));
        F.l18 = t132;
        goto po129;
pa130:;
        uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t134 != ((uint8_t)16ULL)) goto pa133;
        gm_tddtjfggdat0d t135 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)t2) + 8));
        F.l18 = t135;
        goto po129;
pa133:;
        goto mn128;
po129:;
        {
            {
                gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                gm_l20 = t138;
            }
            {
                gm_l21 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t140 = gm_l21;
                    uint64_t t141 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    uint8_t t142 = (t140 < t141);
                    if (!t142) goto lb139;
                    {
                        uint64_t t145 = gm_l21;
                        uint64_t t146 = (uintptr_t)t145;
                        uint64_t t147 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t146 * 4));
                        gm_l22 = t150;
                        {
                            gm_closure t152 = gm_l1;
                            gm_t88rcucg1b7uk t153 = gm_l22;
                            gm_closure t154 = t152;
                            gm_node *ccx155 = t154.cx;
                            void * t156 = t154.code;
                            gm_b0_1 t157;
                            if (GM_PTR_TAGGED(t156)) {
                                ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t156))(GM_CONV(gm_csguankko4lmhd, t153));
                            } else {
                                t157 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t156)(t154.env, ccx155, NULL, t153);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll143;
                            }
                        }
                        goto me148;
mn149:;
                        gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 232, 13);
me148:;
                    }
lc139:;
                    {
                        uint64_t t159 = gm_l21;
                        uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                        uint64_t t162 = r160;
                        gm_l21 = t162;
                    }
                }
gm_ll143:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb139; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc139; }
                }
                goto gm_lad;
lb139:;
            }
        }
        {
            gm_closure t163 = gm_l1;
            gm_t88rcucg1b7uk t164 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l18) + 16));
            gm_closure t165 = t163;
            gm_node *ccx166 = t165.cx;
            void * t167 = t165.code;
            gm_b0_1 t168;
            if (GM_PTR_TAGGED(t167)) {
                ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t167))(GM_CONV(gm_csguankko4lmhd, t164));
            } else {
                t168 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t167)(t165.env, ccx166, NULL, t164);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t170 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l18) + 24) + 0));
                gm_l23 = t170;
            }
            {
                gm_l24 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t172 = gm_l24;
                    uint64_t t173 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                    uint8_t t174 = (t172 < t173);
                    if (!t174) goto lb171;
                    {
                        uint64_t t177 = gm_l24;
                        uint64_t t178 = (uintptr_t)t177;
                        uint64_t t179 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                        gm_tiilmfd16v3fc t182 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t178 * 24));
                        gm_l25 = t182;
                        {
                            gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
                            gm_closure t185 = gm_l1;
                            gm_f_arg_types_645ddbe(t184, t185);
                            if (gm_unw.kind) {
                                goto gm_ll175;
                            }
                        }
                        goto me180;
mn181:;
                        gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 234, 13);
me180:;
                    }
lc171:;
                    {
                        uint64_t t187 = gm_l24;
                        uint64_t r188; bool o189 = __builtin_add_overflow(t187, ((uint64_t)1ULL), &r188);
                        uint64_t t190 = r188;
                        gm_l24 = t190;
                    }
                }
gm_ll175:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb171; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc171; }
                }
                goto gm_lad;
lb171:;
            }
        }
        goto me3;
mn128:;
ma11:;
        uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t192 != ((uint8_t)17ULL)) goto mn191;
        gm_b16_8 t193 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l26 = t193;
        gm_b16_8 t194 = gm_l26;
        gm_closure t195 = gm_l1;
        gm_f_arg_types_645ddbe(t194, t195);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn191:;
ma12:;
        uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t197 != ((uint8_t)24ULL)) goto mn196;
        gm_tnpifv20irnph t198 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)t2) + 8));
        gm_l27 = t198;
        {
            gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 0));
            gm_l28 = t200;
        }
        {
            gm_l29 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t202 = gm_l29;
                uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                uint8_t t204 = (t202 < t203);
                if (!t204) goto lb201;
                {
                    uint64_t t207 = gm_l29;
                    uint64_t t208 = (uintptr_t)t207;
                    uint64_t t209 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                    gm_tiilmfd16v3fc t212 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t208 * 24));
                    gm_l30 = t212;
                    {
                        gm_b16_8 t214 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                        gm_closure t215 = gm_l1;
                        gm_f_arg_types_645ddbe(t214, t215);
                        if (gm_unw.kind) {
                            goto gm_ll205;
                        }
                    }
                    goto me210;
mn211:;
                    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 237, 26);
me210:;
                }
lc201:;
                {
                    uint64_t t217 = gm_l29;
                    uint64_t r218; bool o219 = __builtin_add_overflow(t217, ((uint64_t)1ULL), &r218);
                    uint64_t t220 = r218;
                    gm_l29 = t220;
                }
            }
gm_ll205:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb201; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc201; }
            }
            goto gm_lad;
lb201:;
        }
        goto me3;
mn196:;
ma13:;
        goto me3;
mn221:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 215, 5);
me3:;
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
extern __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) uint32_t gm_f_fresh_effect_2msfvhg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) uint32_t gm_f_fresh_param_442g4m9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_intern_ia19mhl F;
    uint32_t gm_l2; /* id */
    uint32_t gm_l4; /* id */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        void * t3 = F.l0;
        void * t4 = (void *)(((uint8_t *)t3) + 80);
        gm_t9805qtonbsag t5 = gm_f_get_bkkldt5(t4, &(*(gm_t53at80pfdus3 *)((uint8_t *)&F.l1)));
        gm_t9805qtonbsag t6 = t5;
        if (!((((uint8_t *)&t6))[4] != 0)) goto ie2;
        uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&t6));
        gm_l2 = t7;
        {
            uint32_t t9 = gm_l2;
            gm_t88rcucg1b7uk t10;
            GM_ST(uint32_t, ((uint8_t *)&t10 + 0), t9);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t12;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        switch (t19) { case ((uint8_t)0ULL): goto ma17; case ((uint8_t)1ULL): goto ma17; case ((uint8_t)2ULL): goto ma17; case ((uint8_t)3ULL): goto ma17; case ((uint8_t)4ULL): goto ma17; case ((uint8_t)5ULL): goto ma17; case ((uint8_t)6ULL): goto ma17; case ((uint8_t)7ULL): goto ma17; case ((uint8_t)8ULL): goto ma17; case ((uint8_t)9ULL): goto ma17; case ((uint8_t)10ULL): goto ma17; case ((uint8_t)11ULL): goto ma17; case ((uint8_t)12ULL): goto ma17; case ((uint8_t)13ULL): goto ma17; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma17; case ((uint8_t)16ULL): goto ma17; case ((uint8_t)17ULL): goto ma17; case ((uint8_t)18ULL): goto ma17; case ((uint8_t)19ULL): goto ma14; case ((uint8_t)20ULL): goto ma17; case ((uint8_t)21ULL): goto ma17; case ((uint8_t)22ULL): goto ma17; case ((uint8_t)23ULL): goto ma15; case ((uint8_t)24ULL): goto ma17; case ((uint8_t)25ULL): goto ma17; case ((uint8_t)26ULL): goto ma16; default: goto ma17; }
ma14:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t21 != ((uint8_t)19ULL)) goto mn20;
        t12 = ((uint8_t)1ULL);
        goto me13;
mn20:;
ma15:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t23 != ((uint8_t)23ULL)) goto mn22;
        t12 = ((uint8_t)2ULL);
        goto me13;
mn22:;
ma16:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t25 != ((uint8_t)26ULL)) goto mn24;
        t12 = ((uint8_t)4ULL);
        goto me13;
mn24:;
ma17:;
        t12 = ((uint8_t)0ULL);
        goto me13;
mn26:;
ma18:;
        gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 155, 17);
me13:;
        F.l3 = t12;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_closure t28 = ((gm_closure){(void *)gm_f_fn_o0lgah5, (void *)&F.h, cx});
        gm_f_for_each_component_o5cuh7c(t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t29 = F.l0;
        uint64_t t30 = GM_LD(uintptr_t, ((((uint8_t *)t29) + 0) + 0) + 8);
        uint32_t t31 = (uint32_t)t30;
        gm_l4 = t31;
    }
    {
        gm_f_owned_kind_smkp36q((void *)&F.l5, (void *)NULL, cx, NULL, &(*(gm_t53at80pfdus3 *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t32 = F.l0;
        void * t33 = (void *)(((uint8_t *)t32) + 0);
        gm_tftb1odc739t5 t34 = gm_f_push_t1s1chs(t33, &(*(gm_t53at80pfdus3 *)((uint8_t *)&F.l5)));
        gm_tftb1odc739t5 t35 = t34;
        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_src0ih77f43mhf2, 165, 5);
        gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
    }
    {
        void * t37 = F.l0;
        void * t38 = (void *)(((uint8_t *)t37) + 40);
        uint8_t t39 = F.l3;
        gm_tftb1odc739t5 t40 = gm_f_push_t83rg2m(t38, t39);
        gm_tftb1odc739t5 t41 = t40;
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_src0ih77f43mhf2, 166, 5);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
    }
    {
        void * t43 = F.l0;
        void * t44 = (void *)(((uint8_t *)t43) + 80);
        uint32_t t45 = gm_l4;
        gm_tftb1odc739t5 t46 = gm_f_put_nuvto78(t44, &(*(gm_t53at80pfdus3 *)((uint8_t *)&F.l5)), t45);
        gm_tftb1odc739t5 t47 = t46;
        if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_src0ih77f43mhf2, 167, 5);
        gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
    }
    {
        uint32_t t49 = gm_l4;
        gm_t88rcucg1b7uk t50;
        GM_ST(uint32_t, ((uint8_t *)&t50 + 0), t49);
        uint8_t t51 = 0;
        F.ret = t50;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_signed_integer_s66djm7 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint8_t gm_l2; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; default: goto ma9; }
ma7:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t13 != ((uint8_t)0ULL)) goto mn12;
    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
    gm_l2 = t14;
    uint8_t t15 = gm_l2;
    t5 = t15;
    goto me6;
mn12:;
ma8:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t17 != ((uint8_t)2ULL)) goto mn16;
    t5 = ((uint8_t)1);
    goto me6;
mn16:;
ma9:;
    t5 = ((uint8_t)0);
    goto me6;
mn18:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 378, 65);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint32_t gm_f_key_rank_icmtvso(void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_key_u64_ljklb56(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_u64_ljklb56 F;
    gm_t428ue2u3hf5g gm_l0; /* k */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint8_t gm_l6; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 388u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t3 != ((uint8_t)0ULL)) goto ie2;
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 8));
        gm_l1 = t4;
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            {
                gm_b16_8 t7 = gm_l1;
                gm_l3 = t7;
            }
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t9 = gm_l4;
                    uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t11 = (t9 < t10);
                    if (!t11) goto lb8;
                    {
                        uint64_t t14 = gm_l4;
                        uint64_t t15 = gm_l4;
                        uint64_t t16 = (uintptr_t)t15;
                        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t16 * 1));
                        gm_tf5srh4mp4b3i t19;
                        GM_ST(uint8_t, (uint8_t *)&t19 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t19 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t19 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t14);
                        GM_ST(uint8_t, ((uint8_t *)&t19 + 8), t18);
                        gm_tf5srh4mp4b3i t20 = t19;
                        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&t20) + 0));
                        gm_l5 = t23;
                        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t20) + 8));
                        gm_l6 = t24;
                        {
                            uint64_t t27 = gm_l5;
                            uint8_t t28 = (t27 >= ((uint64_t)8ULL));
                            if (!t28) goto ie26;
                            {
                                uint8_t t31 = gm_l6;
                                uint8_t t32 = (!(t31 == ((uint8_t)0ULL)));
                                if (!t32) goto ie30;
                                {
                                    gm_tf5srh4mp4b3i t34;
                                    memset(&t34, 0, sizeof t34);
                                    uint8_t t35 = 0;
                                    F.ret = t34;
                                    goto gm_fret;
                                }
                                goto ix30;
ie30:;
ix30:;
                            }
                            goto ix26;
ie26:;
                            {
                                uint64_t t37 = gm_l2;
                                uint8_t t38 = gm_l6;
                                uint64_t t39 = (uint64_t)t38;
                                uint64_t t40 = gm_l5;
                                uint64_t r41; bool o42 = __builtin_mul_overflow(t40, ((uint64_t)8ULL), &r41);
                                if (o42) gm_trap("integer overflow in `*`", gm_src0ih77f43mhf2, 486, 32);
                                uint64_t t43 = r41;
                                uint64_t t44 = (uint64_t)t43;
                                unsigned __int128 n45 = (unsigned __int128)t44;
                                if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_src0ih77f43mhf2, 486, 18);
                                uint64_t r46 = (uint64_t)((uint64_t)t39 << (int)n45);
                                bool o47 = (uint64_t)(r46 >> (int)n45) != t39;
                                if (o47) gm_trap("integer overflow in `<<`", gm_src0ih77f43mhf2, 486, 18);
                                uint64_t t48 = r46;
                                uint64_t t49 = (uint64_t)(t37 | t48);
                                gm_l2 = t49;
                            }
ix26:;
                        }
                        goto me21;
mn22:;
                        gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 482, 5);
me21:;
                    }
lc8:;
                    {
                        uint64_t t51 = gm_l4;
                        uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                        uint64_t t54 = r52;
                        gm_l4 = t54;
                    }
                }
lb8:;
            }
        }
        {
            uint64_t t55 = gm_l2;
            gm_tf5srh4mp4b3i t56;
            GM_ST(uint8_t, (uint8_t *)&t56 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t56 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t56 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t56), t55);
            ((uint8_t *)&t56)[8] = 1;
            uint8_t t57 = 0;
            F.ret = t56;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_tf5srh4mp4b3i t59;
            memset(&t59, 0, sizeof t59);
            uint8_t t60 = 0;
            F.ret = t59;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_key_u64_ljklb56_c(gm_t428ue2u3hf5g a0) { return gm_f_key_u64_ljklb56(&a0); }
extern __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t53at80pfdus3 gm_f_kind_jcnt4hm_c(void * a0, gm_csguankko4lmhd a1) { gm_t53at80pfdus3 gm_r; gm_f_kind_jcnt4hm(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_len_expr_i134fvg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_len_expr_i134fvg F;
    void * gm_l0; /* t */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    gm_t88rcucg1b7uk gm_l2; /* lhs */
    gm_t88rcucg1b7uk gm_l3; /* rhs */
    gm_t428ue2u3hf5g gm_l4; /* a */
    gm_t428ue2u3hf5g gm_l5; /* b */
    uint64_t gm_l6; /* x */
    uint64_t gm_l7; /* y */
    uint64_t gm_l8; /* n */
    GM_STACK_CHECK(&F, 1098u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t3 = 0;
        void * t4 = gm_l0;
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8 = ((GM_LD(uint8_t, ((uint8_t *)&t7) + 0) == ((uint8_t)26ULL)));
        if (!t8) {
            void * t9 = gm_l0;
            gm_t88rcucg1b7uk t10 = gm_l3;
            gm_t53at80pfdus3 t11;
            gm_f_kind_jcnt4hm((void *)&t11, t9, GM_CONV(gm_csguankko4lmhd, t10));
            gm_t53at80pfdus3 t12 = t11;
            uint8_t t13 = ((GM_LD(uint8_t, ((uint8_t *)&t12) + 0) == ((uint8_t)26ULL)));
            t3 = t13;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            void * t15 = gm_l0;
            gm_t88rcucg1b7uk t16 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l0;
        gm_t88rcucg1b7uk t20 = gm_l2;
        gm_t53at80pfdus3 t21;
        gm_f_kind_jcnt4hm((void *)&t21, t19, GM_CONV(gm_csguankko4lmhd, t20));
        gm_t53at80pfdus3 t22 = t21;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t23 != ((uint8_t)21ULL)) goto ie18;
        gm_t428ue2u3hf5g t24 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&t22) + 8));
        gm_l4 = t24;
        void * t25 = gm_l0;
        gm_t88rcucg1b7uk t26 = gm_l3;
        gm_t53at80pfdus3 t27;
        gm_f_kind_jcnt4hm((void *)&t27, t25, GM_CONV(gm_csguankko4lmhd, t26));
        gm_t53at80pfdus3 t28 = t27;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
        if (t29 != ((uint8_t)21ULL)) goto ie18;
        gm_t428ue2u3hf5g t30 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&t28) + 8));
        gm_l5 = t30;
        {
            gm_tf5srh4mp4b3i t32 = gm_f_key_u64_ljklb56(&(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l4)));
            gm_tf5srh4mp4b3i t33 = t32;
            uint64_t t34;
            if (((((uint8_t *)&t33))[8] != 0)) {
                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&t33) + 0));
                t34 = t35;
            } else {
                void * t36 = gm_l0;
                gm_t88rcucg1b7uk t37 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t38 = 0;
                F.ret = t37;
                goto gm_fret;
            }
            gm_l6 = t34;
        }
        {
            gm_tf5srh4mp4b3i t39 = gm_f_key_u64_ljklb56(&(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l5)));
            gm_tf5srh4mp4b3i t40 = t39;
            uint64_t t41;
            if (((((uint8_t *)&t40))[8] != 0)) {
                uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&t40) + 0));
                t41 = t42;
            } else {
                void * t43 = gm_l0;
                gm_t88rcucg1b7uk t44 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t45 = 0;
                F.ret = t44;
                goto gm_fret;
            }
            gm_l7 = t41;
        }
        {
            gm_t9qkh0rsooc8d t46 = gm_l1;
            uint64_t t47 = gm_l6;
            uint64_t t48 = gm_l7;
            gm_tf5srh4mp4b3i t49 = gm_f_apply_4fi86e0(t46, t47, t48);
            gm_tf5srh4mp4b3i t50 = t49;
            uint64_t t51;
            if (((((uint8_t *)&t50))[8] != 0)) {
                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&t50) + 0));
                t51 = t52;
            } else {
                void * t53 = gm_l0;
                gm_t88rcucg1b7uk t54 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t55 = 0;
                F.ret = t54;
                goto gm_fret;
            }
            gm_l8 = t51;
        }
        {
            void * t56 = gm_l0;
            void * t57 = gm_l0;
            uint64_t t58 = gm_l8;
            gm_t428ue2u3hf5g t59;
            gm_f_usize_key_rs1gk1r((void *)&t59, (void *)NULL, cx, NULL, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t53at80pfdus3 t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t60 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t60 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)21ULL));
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t60 + 8), t59);
            gm_t88rcucg1b7uk t61 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t56, &t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t62 = 0;
            F.ret = t61;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t63 = gm_l0;
        gm_t9qkh0rsooc8d t64 = gm_l1;
        gm_t88rcucg1b7uk t65 = gm_l2;
        gm_t88rcucg1b7uk t66 = gm_l3;
        gm_tcmtrmhj7ak4u t67;
        GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t67 + 0), t64);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t67 + 4), t65);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t67 + 8), t66);
        gm_t53at80pfdus3 t68;
        memset(&t68, 0, sizeof t68);
        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)22ULL));
        GM_ST(gm_tcmtrmhj7ak4u, ((uint8_t *)&t68 + 8), t67);
        gm_t88rcucg1b7uk t69 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t63, &t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t70 = 0;
        F.ret = t69;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_len_op_symbol_2qoiciu(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_len_op_symbol_2qoiciu F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t14) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; default: goto ma13; }
ma3:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)0ULL)) goto mn15;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sajhum9b4nr96));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(1));
    t1 = t17;
    goto me2;
mn15:;
ma4:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)1ULL)) goto mn18;
    gm_b16_8 t20;
    GM_ST(void *, (uint8_t *)&t20, (void *)(gm_skolmnfc0ccbd));
    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
    t1 = t20;
    goto me2;
mn18:;
ma5:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t22 != ((uint8_t)2ULL)) goto mn21;
    gm_b16_8 t23;
    GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s6s96daqmhcs9));
    GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
    t1 = t23;
    goto me2;
mn21:;
ma6:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t25 != ((uint8_t)3ULL)) goto mn24;
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(gm_st3ja0nqi37bv));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(1));
    t1 = t26;
    goto me2;
mn24:;
ma7:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t28 != ((uint8_t)4ULL)) goto mn27;
    gm_b16_8 t29;
    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_siamquc19uikr));
    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(1));
    t1 = t29;
    goto me2;
mn27:;
ma8:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t31 != ((uint8_t)5ULL)) goto mn30;
    gm_b16_8 t32;
    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_si62ejtp0vbct));
    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(2));
    t1 = t32;
    goto me2;
mn30:;
ma9:;
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t34 != ((uint8_t)6ULL)) goto mn33;
    gm_b16_8 t35;
    GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s9hi985r6f61m));
    GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(2));
    t1 = t35;
    goto me2;
mn33:;
ma10:;
    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t37 != ((uint8_t)7ULL)) goto mn36;
    gm_b16_8 t38;
    GM_ST(void *, (uint8_t *)&t38, (void *)(gm_smerqm8g793l6));
    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(1));
    t1 = t38;
    goto me2;
mn36:;
ma11:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t40 != ((uint8_t)8ULL)) goto mn39;
    gm_b16_8 t41;
    GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s2u5posfkbgnq));
    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(1));
    t1 = t41;
    goto me2;
mn39:;
ma12:;
    uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t43 != ((uint8_t)9ULL)) goto mn42;
    gm_b16_8 t44;
    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sa831sfid0hf3));
    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(1));
    t1 = t44;
    goto me2;
mn42:;
ma13:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 352, 45);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
extern __attribute__((aligned(4))) uint8_t gm_f_mentions_error_37knu7r(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_owned_args_q41ihau(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_owned_args_q41ihau F;
    gm_b16_8 gm_l0; /* args */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t428ue2u3hf5g gm_l5; /* v */
    GM_STACK_CHECK(&F, 464u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_vmvl81b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_toa2vhe3l4hfr t15 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 40));
                    F.l4 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l1);
                        gm_toa2vhe3l4hfr t18;
                        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        if (t21 != ((uint8_t)0ULL)) goto mn20;
                        gm_toa2vhe3l4hfr t22 = F.l4;
                        t18 = t22;
                        goto me19;
mn20:;
                        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        if (t24 != ((uint8_t)1ULL)) goto mn23;
                        gm_t428ue2u3hf5g t25 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&F.l4) + 8));
                        gm_l5 = t25;
                        gm_t428ue2u3hf5g t26;
                        gm_f_owned_key_pirfo8n((void *)&t26, (void *)NULL, cx, NULL, &(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l5)));
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_toa2vhe3l4hfr t27;
                        GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t27 + 8), t26);
                        t18 = t27;
                        goto me19;
mn23:;
                        gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 193, 18);
me19:;
                        gm_tftb1odc739t5 t28 = gm_f_push_s5vr2a2(t17, &t18);
                        gm_tftb1odc739t5 t29 = t28;
                        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_src0ih77f43mhf2, 193, 9);
                        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 192, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t32 = gm_l3;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l3 = t35;
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
    {
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t37 = t36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_owned_effects_cesofoh(void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_owned_effects_cesofoh F;
    gm_tnpifv20irnph gm_l0; /* e */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tiilmfd16v3fc gm_l4; /* r */
    GM_STACK_CHECK(&F, 330u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_084fcd6((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l0) + 0));
            gm_l2 = t3;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tiilmfd16v3fc t15 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 24));
                    gm_l4 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l1);
                        gm_tiilmfd16v3fc t18;
                        memset((uint8_t *)&t18 + 4, 0, 4);
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
                        GM_ST(uint32_t, (uint8_t *)&t18 + 0, t19);
                        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                        gm_b16_8 t21 = gm_f_owned_args_q41ihau((void *)NULL, cx, NULL, t20);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t18 + 8, t21);
                        gm_tftb1odc739t5 t22 = gm_f_push_d1r4h19(t17, &t18);
                        gm_tftb1odc739t5 t23 = t22;
                        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_src0ih77f43mhf2, 186, 26);
                        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 186, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t26 = gm_l3;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l3 = t29;
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
    {
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t31 = t30;
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        gm_tnpifv20irnph t33;
        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
        uint8_t t34 = 0;
        F.ret = t33;
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
extern __attribute__((aligned(4))) void gm_f_owned_fn_ket04lo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_owned_key_pirfo8n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_owned_key_pirfo8n F;
    gm_t428ue2u3hf5g gm_l0; /* v */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* bytes */
    gm_b16_8 gm_l3; /* bytes */
    GM_STACK_CHECK(&F, 327u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t428ue2u3hf5g t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma4; default: goto ma5; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)a0) + 8) + 0));
    gm_l1 = t10;
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 8));
    gm_l2 = t11;
    gm_tcmkompbl09il t12;
    memset((uint8_t *)&t12 + 4, 0, 4);
    gm_t88rcucg1b7uk t13 = gm_l1;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t12 + 0, t13);
    gm_b16_8 t14 = gm_l2;
    gm_b16_8 t15 = gm_f_copied_hn10ogl((void *)NULL, cx, NULL, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t12 + 8, t15);
    gm_t428ue2u3hf5g t16;
    GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t16 + 8), t12);
    t1 = t16;
    goto me2;
mn8:;
ma4:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t18 != ((uint8_t)3ULL)) goto mn17;
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l3 = t19;
    gm_b16_8 t20 = gm_l3;
    gm_b16_8 t21 = gm_f_copied_hn10ogl((void *)NULL, cx, NULL, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t428ue2u3hf5g t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t22 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)3ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t22 + 8), t21);
    t1 = t22;
    goto me2;
mn17:;
ma5:;
    gm_t428ue2u3hf5g t24 = GM_LD(gm_t428ue2u3hf5g, ((uint8_t *)a0));
    t1 = t24;
    goto me2;
mn23:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 201, 57);
me2:;
    F.ret = t1;
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
inline __attribute__((aligned(4))) void gm_f_owned_kind_smkp36q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_owned_kind_smkp36q F;
    gm_b16_8 gm_l1; /* elems */
    gm_b16_8 gm_l2; /* names */
    uint32_t gm_l5; /* id */
    gm_b16_8 gm_l6; /* args */
    gm_t428ue2u3hf5g gm_l7; /* v */
    gm_tnpifv20irnph gm_l8; /* e */
    GM_STACK_CHECK(&F, 1067u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_t53at80pfdus3 t1;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma3; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; default: goto ma9; }
ma3:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    if (t13 != ((uint8_t)12ULL)) goto mn12;
    gm_b16_8 t14 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_l1 = t14;
    gm_b16_8 t15 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 16));
    gm_l2 = t15;
    gm_t6j6smj7qoctu t16;
    gm_b16_8 t17 = gm_l1;
    gm_b16_8 t18 = gm_f_copied_u3c68o3((void *)NULL, cx, NULL, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t16 + 0, t18);
    gm_b16_8 t19 = gm_l2;
    gm_b16_8 t20 = gm_f_copied_nbtatio((void *)NULL, cx, NULL, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t16 + 16, t20);
    gm_t53at80pfdus3 t21;
    GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t21 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t21 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t21 + 8), t16);
    t1 = t21;
    goto me2;
mn12:;
ma4:;
    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    if (t23 != ((uint8_t)15ULL)) goto mn22;
    gm_tddtjfggdat0d t24 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&F.l0) + 8));
    F.l3 = t24;
    gm_tddtjfggdat0d t25;
    gm_f_owned_fn_ket04lo((void *)&t25, (void *)NULL, cx, NULL, &(*(gm_tddtjfggdat0d *)((uint8_t *)&F.l3)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t53at80pfdus3 t26;
    GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)15ULL));
    GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t26 + 8), t25);
    t1 = t26;
    goto me2;
mn22:;
ma5:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    if (t28 != ((uint8_t)16ULL)) goto mn27;
    gm_tddtjfggdat0d t29 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&F.l0) + 8));
    F.l4 = t29;
    gm_tddtjfggdat0d t30;
    gm_f_owned_fn_ket04lo((void *)&t30, (void *)NULL, cx, NULL, &(*(gm_tddtjfggdat0d *)((uint8_t *)&F.l4)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t53at80pfdus3 t31;
    GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)16ULL));
    GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t31 + 8), t30);
    t1 = t31;
    goto me2;
mn27:;
ma6:;
    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    if (t33 != ((uint8_t)17ULL)) goto mn32;
    uint32_t t34 = GM_LD(uint32_t, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_l5 = t34;
    gm_b16_8 t35 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 8));
    gm_l6 = t35;
    gm_tiilmfd16v3fc t36;
    memset((uint8_t *)&t36 + 4, 0, 4);
    uint32_t t37 = gm_l5;
    GM_ST(uint32_t, (uint8_t *)&t36 + 0, t37);
    gm_b16_8 t38 = gm_l6;
    gm_b16_8 t39 = gm_f_owned_args_q41ihau((void *)NULL, cx, NULL, t38);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t36 + 8, t39);
    gm_t53at80pfdus3 t40;
    GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t40 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t40 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t40 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)17ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t40 + 8), t36);
    t1 = t40;
    goto me2;
mn32:;
ma7:;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    if (t42 != ((uint8_t)21ULL)) goto mn41;
    gm_t428ue2u3hf5g t43 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&F.l0) + 8));
    gm_l7 = t43;
    gm_t428ue2u3hf5g t44;
    gm_f_owned_key_pirfo8n((void *)&t44, (void *)NULL, cx, NULL, &(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l7)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t53at80pfdus3 t45;
    GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t45 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t45 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)21ULL));
    GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t45 + 8), t44);
    t1 = t45;
    goto me2;
mn41:;
ma8:;
    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    if (t47 != ((uint8_t)24ULL)) goto mn46;
    gm_tnpifv20irnph t48 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l0) + 8));
    gm_l8 = t48;
    gm_tnpifv20irnph t49 = gm_l8;
    gm_tnpifv20irnph t50 = gm_f_owned_effects_cesofoh((void *)NULL, cx, NULL, t49);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t53at80pfdus3 t51;
    memset(&t51, 0, sizeof t51);
    GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)24ULL));
    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t51 + 8), t50);
    t1 = t51;
    goto me2;
mn46:;
ma9:;
    gm_t53at80pfdus3 t53 = F.l0;
    t1 = t53;
    goto me2;
mn52:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 172, 54);
me2:;
    F.ret = t1;
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
extern __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_splice_tj8iedl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_splice_tj8iedl F;
    gm_t88rcucg1b7uk gm_l0; /* p */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_vmvl81b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_t88rcucg1b7uk t3 = gm_l0;
        gm_toa2vhe3l4hfr t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 8), t3);
        gm_tftb1odc739t5 t5 = gm_f_push_s5vr2a2(t2, &t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_src0ih77f43mhf2, 469, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        memset((uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 4, 0, 4);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 0, ((uint32_t)0ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 8, t10);
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_tuple_l3i6trt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_types_8a61ga3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_types_8a61ga3 F;
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_ri1fn5p((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l0 + 0)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_uvfu1k1((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l0 + 40)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_4s138tg((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l0 + 80)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l0 + 136)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l0 + 176)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l0 + 216)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l0 + 256)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = (void *)((uint8_t *)&F.l0);
            gm_b16_8 t4;
            GM_ST(void *, (uint8_t *)&t4, (void *)(gm_snenkhfev166s));
            GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(2));
            uint32_t t5 = gm_f_fresh_effect_2msfvhg((void *)NULL, cx, NULL, t3, t4);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l0), 296);
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_usize_key_rs1gk1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_usize_key_rs1gk1r F;
    void * gm_l0; /* t */
    uint64_t gm_l1; /* n */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 259u);
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
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            gm_l3 = ((uint64_t)8ULL);
        }
        {
            for (;;) {
                uint64_t t4 = gm_l4;
                uint64_t t5 = gm_l3;
                uint8_t t6 = (t4 < t5);
                if (!t6) goto lb3;
                {
                    uint64_t t9 = gm_l4;
                    uint64_t t10 = t9;
                    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&t10));
                    gm_l5 = t13;
                    {
                        void * t15 = (void *)((uint8_t *)&F.l2);
                        uint64_t t16 = gm_l1;
                        uint64_t t17 = gm_l5;
                        uint64_t r18; bool o19 = __builtin_mul_overflow(t17, ((uint64_t)8ULL), &r18);
                        if (o19) gm_trap("integer overflow in `*`", gm_src0ih77f43mhf2, 332, 42);
                        uint64_t t20 = r18;
                        unsigned __int128 n21 = (unsigned __int128)t20;
                        if (n21 >= 64) gm_trap_shift((uint64_t)n21, "u64", gm_src0ih77f43mhf2, 332, 33);
                        uint64_t t22 = (uint64_t)(t16 >> (int)n21);
                        uint8_t t23 = (uint8_t)t22;
                        gm_tftb1odc739t5 t24 = gm_f_push_t83rg2m(t15, t23);
                        gm_tftb1odc739t5 t25 = t24;
                        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_src0ih77f43mhf2, 332, 21);
                        gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 332, 5);
me11:;
                }
lc3:;
                {
                    uint64_t t28 = gm_l4;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l4 = t31;
                }
            }
lb3:;
        }
    }
    {
        gm_tcmkompbl09il t32;
        memset((uint8_t *)&t32 + 4, 0, 4);
        void * t33 = gm_l0;
        gm_t88rcucg1b7uk t34 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t32 + 0, t34);
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t36 = t35;
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        GM_ST(gm_b16_8, (uint8_t *)&t32 + 8, t37);
        gm_t428ue2u3hf5g t38;
        GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t38 + 8), t32);
        uint8_t t39 = 0;
        F.ret = t38;
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
