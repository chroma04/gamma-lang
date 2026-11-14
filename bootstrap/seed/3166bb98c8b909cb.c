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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_taqu3436kubri;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
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
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; gm_tl874t2atnlh3 f3; } gm_tgk5j0pob2pqa;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; gm_tkn42u7na3t81 v1; } f1; } gm_t1qr0p6kln6tf;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_taqu3436kubri) == 20 && _Alignof(gm_taqu3436kubri) == 4 && offsetof(gm_taqu3436kubri, f0) == 0 && offsetof(gm_taqu3436kubri, f1) == 4 && offsetof(gm_taqu3436kubri, f2) == 8, "layout of `HandlerBreak`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tgk5j0pob2pqa) == 24 && _Alignof(gm_tgk5j0pob2pqa) == 4 && offsetof(gm_tgk5j0pob2pqa, f0) == 0 && offsetof(gm_tgk5j0pob2pqa, f1) == 4 && offsetof(gm_tgk5j0pob2pqa, f2) == 8 && offsetof(gm_tgk5j0pob2pqa, f3) == 12, "layout of `LabelBreak`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t pe[4]; } gm_tuanp0jbrlb6j;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1qr0p6kln6tf) == 12 && _Alignof(gm_t1qr0p6kln6tf) == 4 && offsetof(gm_t1qr0p6kln6tf, f0) == 0 && offsetof(gm_t1qr0p6kln6tf, f1) == 4, "layout of `LabelValue`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t f2; uint8_t p3[3]; gm_t1qr0p6kln6tf f3; uint8_t pe[4]; } gm_t8enl5g7b22jb;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { gm_tuanp0jbrlb6j f0; uint8_t f1; uint8_t pe[7]; } gm_tjvtgq0p0sehv;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tuanp0jbrlb6j) == 32 && _Alignof(gm_tuanp0jbrlb6j) == 8 && offsetof(gm_tuanp0jbrlb6j, f0) == 0 && offsetof(gm_tuanp0jbrlb6j, f1) == 24, "layout of `ForLabel`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t8enl5g7b22jb) == 48 && _Alignof(gm_t8enl5g7b22jb) == 8 && offsetof(gm_t8enl5g7b22jb, f0) == 0 && offsetof(gm_t8enl5g7b22jb, f1) == 24 && offsetof(gm_t8enl5g7b22jb, f2) == 28 && offsetof(gm_t8enl5g7b22jb, f3) == 32, "layout of `LabelInfo`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t8enl5g7b22jb f1; } gm_ti8jkg54tfeop;
_Static_assert(sizeof(gm_tjvtgq0p0sehv) == 40 && _Alignof(gm_tjvtgq0p0sehv) == 8 && offsetof(gm_tjvtgq0p0sehv, f0) == 0 && offsetof(gm_tjvtgq0p0sehv, f1) == 32, "layout of `?ForLabel`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_tkn42u7na3t81 f3; uint8_t p4[4]; gm_tjvtgq0p0sehv f4; gm_tkn42u7na3t81 f5; gm_tqu3m2l3n8mnc f6; } gm_tnqmlaad6uchl;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tnqmlaad6uchl f0; uint8_t f1; uint8_t pe[7]; } gm_t403eokj7ifg9;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_ti8jkg54tfeop) == 56 && _Alignof(gm_ti8jkg54tfeop) == 8 && offsetof(gm_ti8jkg54tfeop, f0) == 0 && offsetof(gm_ti8jkg54tfeop, f1) == 8, "layout of `(levels: u32, info: LabelInfo)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ti8jkg54tfeop v0; uint32_t v1; } f1; } gm_tl9o0tv3f6gtf;
_Static_assert(sizeof(gm_tnqmlaad6uchl) == 112 && _Alignof(gm_tnqmlaad6uchl) == 8 && offsetof(gm_tnqmlaad6uchl, f0) == 0 && offsetof(gm_tnqmlaad6uchl, f1) == 4 && offsetof(gm_tnqmlaad6uchl, f2) == 12 && offsetof(gm_tnqmlaad6uchl, f3) == 20 && offsetof(gm_tnqmlaad6uchl, f4) == 32 && offsetof(gm_tnqmlaad6uchl, f5) == 72 && offsetof(gm_tnqmlaad6uchl, f6) == 80, "layout of `ClauseInfo`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_t403eokj7ifg9) == 120 && _Alignof(gm_t403eokj7ifg9) == 8 && offsetof(gm_t403eokj7ifg9, f0) == 0 && offsetof(gm_t403eokj7ifg9, f1) == 112, "layout of `?ClauseInfo`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tl9o0tv3f6gtf) == 64 && _Alignof(gm_tl9o0tv3f6gtf) == 8 && offsetof(gm_tl9o0tv3f6gtf, f0) == 0 && offsetof(gm_tl9o0tv3f6gtf, f1) == 8, "layout of `Jump`");
typedef struct { gm_tl9o0tv3f6gtf f0; uint8_t f1; uint8_t pe[7]; } gm_tt5b9omhp87j8;
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_tt5b9omhp87j8) == 72 && _Alignof(gm_tt5b9omhp87j8) == 8 && offsetof(gm_tt5b9omhp87j8, f0) == 0 && offsetof(gm_tt5b9omhp87j8, f1) == 64, "layout of `?Jump`");
struct gm_fr_break_expr_l653dlv {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_tl9o0tv3f6gtf l4; /* jump */
    gm_t8enl5g7b22jb l7; /* info */
    gm_tnqmlaad6uchl l18; /* c */
    void * ret;
};
struct gm_fr_break_value_nkons87 {
    gm_frame h;
    void * ret;
};
struct gm_fr_check_fn_clause_exit_5an7bnc {
    gm_frame h;
    gm_tnqmlaad6uchl l5; /* c */
    gm_tnqmlaad6uchl l10; /* c */
    gm_b0_1 ret;
};
struct gm_fr_continue_expr_53f04kf {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_t8enl5g7b22jb l5; /* info */
    void * ret;
};
struct gm_fr_depth_fr7pn6n {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
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
struct gm_fr_empty_slice_rcd2jln {
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
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_find_jump_4qhhk7u {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_t8enl5g7b22jb l5; /* info */
    gm_tnqmlaad6uchl l7; /* clause */
    gm_t403eokj7ifg9 l10; /* frame_clause */
    gm_tnqmlaad6uchl l13; /* c */
    gm_t8enl5g7b22jb l18; /* info */
    gm_tt5b9omhp87j8 ret;
};
struct gm_fr_find_label_gtm0hva {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_t8enl5g7b22jb l3; /* x */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_open_return_5q4g4bo {
    gm_frame h;
    void * ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_return_expr_0qs023i {
    gm_frame h;
    void * ret;
};
struct gm_fr_return_target_ijqt4mp {
    gm_frame h;
    gm_tplpbqf3duvn3 ret;
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
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ohk62mc {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_r3r7qdh {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_break_expr_l653dlv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_break_value_nkons87(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_fn_clause_exit_5an7bnc(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_continue_expr_53f04kf(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) uint32_t gm_f_depth_fr7pn6n(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) void gm_f_find_jump_4qhhk7u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2, uint8_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_label_gtm0hva(gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_label_gtm0hva_c(gm_b16_8 a0, gm_t1s75i08qtptd a1);
__attribute__((aligned(4))) void gm_f_format_8ubh5ip(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_cgo7upp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hqail32(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_j8m87ti(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mkrut0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_open_return_5q4g4bo(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_26bmlpd(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_gfi6h9h(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_usak761(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_return_expr_0qs023i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tplpbqf3duvn3 gm_f_return_target_ijqt4mp(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern const uint8_t gm_s277jq8ulrihr[7];
extern const uint8_t gm_s8av3ru5o85v4[6];
extern const uint8_t gm_sa9mckopi2o5a[86];
extern const uint8_t gm_ska00mcp5pgpq[53];
extern const uint8_t gm_sm9iv4f7cjs9t[62];
extern const uint8_t gm_sn4e8vibva5m7[45];
extern const uint8_t gm_soc64bhm712u9[65];
extern const uint8_t gm_sqdjjj1odavib[34];
extern const uint8_t gm_sr31o7m0aohvd[9];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcici0r1ok5poc[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srjh1vh9n4las[87];
extern const uint8_t gm_svn0tfk5ehe5n[48];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b) {
    if (a[16] != b[16]) return false;
    return !a[16] || gm_eq0iqiubfm7e(a, b);
}
inline __attribute__((aligned(4))) uint32_t gm_f_depth_fr7pn6n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_depth_fr7pn6n F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)t1) + 576) + 0) + 8);
    uint32_t t3 = (uint32_t)t2;
    F.ret = t3;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_error_expr_o6qt7e2 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_f_empty_slice_g3tddrb_2();
    gm_teti411eujvf4 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_tl874t2atnlh3 t6 = gm_l1;
    void * t7 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t3, &t5, t6);
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 176u);
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
    gm_tq7s98ihonkju t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t6 = GM_LD(void *, (((uint8_t *)&t5) + 8));
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
    GM_STACK_CHECK(&F, 145u);
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
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_unit_ohk62mc F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_f_empty_slice_g3tddrb_2();
    gm_teti411eujvf4 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_tl874t2atnlh3 t6 = gm_l1;
    void * t7 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t3, &t5, t6);
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
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_r3r7qdh F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 133u);
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
        gm_b0_1 t6 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_tl874t2atnlh3 t9 = gm_l1;
        void * t10 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = 0;
        F.ret = t10;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_t6j6smj7qoctu t4;
        gm_b16_8 t5 = GM_LD(gm_b16_8, gm_c3t36dlh4g5);
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tabb710d9mq2e t7;
        gm_f_format_e79cvtl((void *)&t7, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_srclv0dcbbfhves, 635, 19);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
        gm_b16_8 t10 = t9;
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        gm_b0_1 t12 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t11);
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
__attribute__((aligned(4))) void * gm_f_break_expr_l653dlv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_break_expr_l653dlv F;
    void * gm_l0; /* cx */
    void * gm_l2; /* value */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t88rcucg1b7uk gm_l5; /* nv */
    uint32_t gm_l6; /* levels */
    uint32_t gm_l8; /* depth */
    void * gm_l9; /* v */
    void * gm_l10; /* x */
    gm_t88rcucg1b7uk gm_l11; /* rt */
    void * gm_l12; /* x */
    gm_t88rcucg1b7uk gm_l13; /* t */
    gm_tkn42u7na3t81 gm_l14; /* expect */
    void * gm_l15; /* x */
    gm_t88rcucg1b7uk gm_l16; /* t */
    gm_t88rcucg1b7uk gm_l17; /* ty */
    gm_t88rcucg1b7uk gm_l19; /* rt */
    void * gm_l20; /* v */
    void * gm_l21; /* x */
    void * gm_l22; /* v */
    void * gm_l23; /* x */
    gm_t88rcucg1b7uk gm_l24; /* t */
    gm_t88rcucg1b7uk gm_l25; /* ty */
    uint32_t gm_l26; /* h */
    uint32_t gm_l27; /* f */
    GM_STACK_CHECK(&F, 2963u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l3;
        gm_tt5b9omhp87j8 t4;
        gm_f_find_jump_4qhhk7u((void *)&t4, (void *)NULL, cx, NULL, t2, &(*(gm_t1s75i08qtptd *)((uint8_t *)a1)), t3, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tt5b9omhp87j8 t5 = t4;
        gm_tl9o0tv3f6gtf t6;
        if (((((uint8_t *)&t5))[64] != 0)) {
            gm_tl9o0tv3f6gtf t7 = GM_LD(gm_tl9o0tv3f6gtf, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            void * t8 = gm_l0;
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
            gm_tl874t2atnlh3 t10 = gm_l3;
            void * t11 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        F.l4 = t6;
    }
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t16;
    }
    {
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        switch (t22) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma20; default: goto ma21; }
ma18:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t24 != ((uint8_t)0ULL)) goto mn23;
        uint32_t t25 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l6 = t25;
        gm_t8enl5g7b22jb t26 = GM_LD(gm_t8enl5g7b22jb, ((((uint8_t *)&F.l4) + 8) + 8));
        F.l7 = t26;
        {
            void * t28 = gm_l0;
            uint32_t t29 = gm_f_depth_fr7pn6n(t28);
            uint32_t t30 = gm_l6;
            uint32_t r31; bool o32 = __builtin_sub_overflow(t29, t30, &r31);
            if (o32) gm_trap("integer overflow in `-`", gm_srcici0r1ok5poc, 163, 22);
            uint32_t t33 = r31;
            gm_l8 = t33;
        }
        {
            gm_l9 = ((void *)0);
        }
        {
            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 32) + 0));
            switch (t39) { case ((uint8_t)0ULL): goto ma35; case ((uint8_t)1ULL): goto ma36; case ((uint8_t)2ULL): goto ma37; default: goto ma38; }
ma35:;
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 32) + 0));
            if (t41 != ((uint8_t)0ULL)) goto mn40;
            {
                void * t43 = gm_l0;
                void * t44 = (void *)(((uint8_t *)t43) + 432);
                gm_tgk5j0pob2pqa t45;
                uint32_t t46 = gm_l8;
                GM_ST(uint32_t, (uint8_t *)&t45 + 0, t46);
                uint32_t t47 = GM_LD(uint32_t, (((uint8_t *)&F.l7) + 24));
                GM_ST(uint32_t, (uint8_t *)&t45 + 4, t47);
                void * t48 = gm_l0;
                void * t49 = GM_LD(void *, (((uint8_t *)t48) + 0));
                void * t50 = (void *)(((uint8_t *)t49) + 8);
                gm_t88rcucg1b7uk t51 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t45 + 8, t51);
                gm_tl874t2atnlh3 t52 = gm_l3;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t45 + 12, t52);
                gm_tftb1odc739t5 t53 = gm_f_push_usak761(t44, &t45);
                gm_tftb1odc739t5 t54 = t53;
                if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcici0r1ok5poc, 167, 21);
                gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
            }
            {
                void * t57 = gm_l2;
                void * t58 = t57;
                if (!(GM_LD(void *, ((uint8_t *)&t58)) != 0)) goto ie56;
                void * t59 = GM_LD(void *, ((uint8_t *)&t58));
                gm_l10 = t59;
                {
                    void * t61 = gm_l0;
                    void * t62 = gm_l10;
                    gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t62) + 72));
                    gm_b16_8 t64;
                    GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sm9iv4f7cjs9t));
                    GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(61));
                    gm_b0_1 t65 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t61, t63, t64);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix56;
ie56:;
ix56:;
            }
            goto me34;
mn40:;
ma36:;
            uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 32) + 0));
            if (t67 != ((uint8_t)1ULL)) goto mn66;
            gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l7) + 32) + 4));
            gm_l11 = t68;
            {
                void * t70 = gm_l0;
                void * t71 = (void *)(((uint8_t *)t70) + 432);
                gm_tgk5j0pob2pqa t72;
                uint32_t t73 = gm_l8;
                GM_ST(uint32_t, (uint8_t *)&t72 + 0, t73);
                uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)&F.l7) + 24));
                GM_ST(uint32_t, (uint8_t *)&t72 + 4, t74);
                void * t75 = gm_l0;
                void * t76 = GM_LD(void *, (((uint8_t *)t75) + 0));
                void * t77 = (void *)(((uint8_t *)t76) + 8);
                gm_t88rcucg1b7uk t78 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t72 + 8, t78);
                gm_tl874t2atnlh3 t79 = gm_l3;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t72 + 12, t79);
                gm_tftb1odc739t5 t80 = gm_f_push_usak761(t71, &t72);
                gm_tftb1odc739t5 t81 = t80;
                if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srcici0r1ok5poc, 172, 21);
                gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
            }
            {
                void * t84 = gm_l2;
                void * t85 = t84;
                if (!(GM_LD(void *, ((uint8_t *)&t85)) != 0)) goto ie83;
                void * t86 = GM_LD(void *, ((uint8_t *)&t85));
                gm_l12 = t86;
                {
                    void * t88 = gm_l0;
                    void * t89 = gm_l0;
                    void * t90 = gm_l12;
                    gm_t88rcucg1b7uk t91 = gm_l11;
                    gm_tkn42u7na3t81 t92;
                    GM_ST(uint8_t, (uint8_t *)&t92 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t92 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t92), t91);
                    ((uint8_t *)&t92)[4] = 1;
                    void * t93 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t89, t90, t92);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t94 = gm_l11;
                    void * t95 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t88, t93, t94);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l9 = t95;
                }
                goto ix83;
ie83:;
                void * t97 = gm_l0;
                void * t98 = GM_LD(void *, (((uint8_t *)t97) + 0));
                void * t99 = (void *)(((uint8_t *)t98) + 8);
                gm_t88rcucg1b7uk t100 = gm_l11;
                uint8_t t101 = gm_f_is_unit_n83ttnb(t99, GM_CONV(gm_csguankko4lmhd, t100));
                uint8_t t102 = (uint8_t)!t101;
                if (!t102) goto ie96;
                void * t103 = gm_l0;
                void * t104 = GM_LD(void *, (((uint8_t *)t103) + 0));
                void * t105 = (void *)(((uint8_t *)t104) + 8);
                gm_t88rcucg1b7uk t106 = gm_l11;
                uint8_t t107 = gm_f_is_error_8k27gd1(t105, GM_CONV(gm_csguankko4lmhd, t106));
                uint8_t t108 = (uint8_t)!t107;
                if (!t108) goto ie96;
                {
                    void * t110 = gm_l0;
                    gm_tl874t2atnlh3 t111 = gm_l3;
                    void * t112 = gm_l0;
                    gm_t88rcucg1b7uk t113 = gm_l11;
                    gm_b16_8 t114 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t112, t113);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t115;
                    GM_ST(gm_b16_8, ((uint8_t *)&t115 + 0), t114);
                    gm_tabb710d9mq2e t116;
                    gm_f_format_mkrut0g((void *)&t116, (void *)NULL, cx, NULL, t115);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t117 = t116;
                    if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 8, gm_srcici0r1ok5poc, 176, 40);
                    gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
                    gm_b16_8 t119 = t118;
                    gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
                    gm_b0_1 t121 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t110, t111, t120);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix96;
ie96:;
ix96:;
ix83:;
            }
            goto me34;
mn66:;
ma37:;
            uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 32) + 0));
            if (t123 != ((uint8_t)2ULL)) goto mn122;
            {
                gm_tkn42u7na3t81 t125;
                uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 32) + 0));
                if (t127 != ((uint8_t)2ULL)) goto ie126;
                if (!((((((uint8_t *)&F.l7) + 32) + 4))[4] != 0)) goto ie126;
                gm_t88rcucg1b7uk t128 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l7) + 32) + 4));
                gm_l13 = t128;
                gm_t88rcucg1b7uk t130 = gm_l13;
                gm_t88rcucg1b7uk t131 = t130;
                gm_tkn42u7na3t81 t132;
                GM_ST(uint8_t, (uint8_t *)&t132 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t132 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t132), t131);
                ((uint8_t *)&t132)[4] = 1;
                t125 = t132;
                goto ix126;
ie126:;
                gm_tkn42u7na3t81 t134;
                memset(&t134, 0, sizeof t134);
                gm_tkn42u7na3t81 t135 = t134;
                t125 = t135;
ix126:;
                gm_l14 = t125;
            }
            {
                void * t136 = gm_l0;
                void * t137 = gm_l2;
                gm_tkn42u7na3t81 t138 = gm_l14;
                void * t139 = gm_f_break_value_nkons87((void *)NULL, cx, NULL, t136, t137, t138);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t139;
            }
            {
                gm_t88rcucg1b7uk t140;
                void * t142 = gm_l9;
                void * t143 = t142;
                if (!(GM_LD(void *, ((uint8_t *)&t143)) != 0)) goto ie141;
                void * t144 = GM_LD(void *, ((uint8_t *)&t143));
                gm_l15 = t144;
                gm_t88rcucg1b7uk t146;
                if (!((((uint8_t *)&gm_l14))[4] != 0)) goto ie147;
                gm_t88rcucg1b7uk t148 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l14));
                gm_l16 = t148;
                gm_t88rcucg1b7uk t150 = gm_l16;
                gm_t88rcucg1b7uk t151 = t150;
                t146 = t151;
                goto ix147;
ie147:;
                void * t153 = gm_l15;
                gm_t88rcucg1b7uk t154 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t153) + 0));
                gm_t88rcucg1b7uk t155 = t154;
                t146 = t155;
ix147:;
                gm_t88rcucg1b7uk t156 = t146;
                t140 = t156;
                goto ix141;
ie141:;
                void * t158 = gm_l0;
                void * t159 = GM_LD(void *, (((uint8_t *)t158) + 0));
                void * t160 = (void *)(((uint8_t *)t159) + 8);
                gm_t88rcucg1b7uk t161 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t160);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t162 = t161;
                t140 = t162;
ix141:;
                gm_l17 = t140;
            }
            {
                void * t163 = gm_l0;
                void * t164 = (void *)(((uint8_t *)t163) + 432);
                gm_tgk5j0pob2pqa t165;
                uint32_t t166 = gm_l8;
                GM_ST(uint32_t, (uint8_t *)&t165 + 0, t166);
                uint32_t t167 = GM_LD(uint32_t, (((uint8_t *)&F.l7) + 24));
                GM_ST(uint32_t, (uint8_t *)&t165 + 4, t167);
                gm_t88rcucg1b7uk t168 = gm_l17;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t165 + 8, t168);
                gm_tl874t2atnlh3 t169 = gm_l3;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t165 + 12, t169);
                gm_tftb1odc739t5 t170 = gm_f_push_usak761(t164, &t165);
                gm_tftb1odc739t5 t171 = t170;
                if (((((uint8_t *)&t171))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t171) + 1, gm_srcici0r1ok5poc, 184, 21);
                gm_b0_1 t172 = GM_LD(gm_b0_1, (((uint8_t *)&t171) + 1));
            }
            goto me34;
mn122:;
ma38:;
            gm_trap("no match arm matched the scrutinee", gm_srcici0r1ok5poc, 165, 13);
me34:;
        }
        {
            gm_t88rcucg1b7uk t173 = gm_l5;
            gm_teo9d4mncjfdu t174;
            memset((uint8_t *)&t174 + 12, 0, 4);
            uint32_t t175 = gm_l6;
            uint32_t t176 = GM_LD(uint32_t, (((uint8_t *)&F.l7) + 24));
            gm_tcc1rq4n1avb2 t177;
            GM_ST(uint32_t, ((uint8_t *)&t177 + 0), t175);
            GM_ST(uint32_t, ((uint8_t *)&t177 + 4), t176);
            gm_te9hrk0u35rev t178;
            GM_ST(uint8_t, (uint8_t *)&t178 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t178 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t178 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t178 + 4), t177);
            GM_ST(gm_te9hrk0u35rev, (uint8_t *)&t174 + 0, t178);
            void * t179 = gm_l9;
            GM_ST(void *, (uint8_t *)&t174 + 16, t179);
            gm_teti411eujvf4 t180;
            memset(&t180, 0, sizeof t180);
            GM_ST(uint8_t, ((uint8_t *)&t180 + 0), ((uint8_t)49ULL));
            GM_ST(gm_teo9d4mncjfdu, ((uint8_t *)&t180 + 8), t174);
            gm_tl874t2atnlh3 t181 = gm_l3;
            void * t182 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t173, &t180, t181);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t183 = 0;
            F.ret = t182;
            goto gm_fret;
        }
        goto me17;
mn23:;
ma19:;
        uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t185 != ((uint8_t)1ULL)) goto mn184;
        {
            void * t187 = gm_l0;
            gm_t403eokj7ifg9 t188 = GM_LD(gm_t403eokj7ifg9, (((uint8_t *)t187) + 800));
            gm_t403eokj7ifg9 t189 = t188;
            gm_tnqmlaad6uchl t190;
            if (((((uint8_t *)&t189))[112] != 0)) {
                gm_tnqmlaad6uchl t191 = GM_LD(gm_tnqmlaad6uchl, (((uint8_t *)&t189) + 0));
                t190 = t191;
            } else {
                void * t192 = gm_l0;
                void * t193 = GM_LD(void *, (((uint8_t *)t192) + 0));
                gm_tl874t2atnlh3 t194 = gm_l3;
                void * t195 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t193, t194);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t196 = 0;
                F.ret = t195;
                goto gm_fret;
            }
            F.l18 = t190;
        }
        {
            if (!(((((uint8_t *)&F.l18) + 72))[4] != 0)) goto ie197;
            gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l18) + 72));
            gm_l19 = t198;
            {
                gm_l20 = ((void *)0);
            }
            {
                void * t201 = gm_l2;
                void * t202 = t201;
                if (!(GM_LD(void *, ((uint8_t *)&t202)) != 0)) goto ie200;
                void * t203 = GM_LD(void *, ((uint8_t *)&t202));
                gm_l21 = t203;
                {
                    void * t205 = gm_l0;
                    void * t206 = gm_l0;
                    void * t207 = gm_l21;
                    gm_t88rcucg1b7uk t208 = gm_l19;
                    gm_tkn42u7na3t81 t209;
                    GM_ST(uint8_t, (uint8_t *)&t209 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t209 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t209), t208);
                    ((uint8_t *)&t209)[4] = 1;
                    void * t210 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t206, t207, t209);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t211 = gm_l19;
                    void * t212 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t205, t210, t211);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l20 = t212;
                }
                goto ix200;
ie200:;
                void * t214 = gm_l0;
                void * t215 = GM_LD(void *, (((uint8_t *)t214) + 0));
                void * t216 = (void *)(((uint8_t *)t215) + 8);
                gm_t88rcucg1b7uk t217 = gm_l19;
                uint8_t t218 = gm_f_is_unit_n83ttnb(t216, GM_CONV(gm_csguankko4lmhd, t217));
                uint8_t t219 = (uint8_t)!t218;
                if (!t219) goto ie213;
                void * t220 = gm_l0;
                void * t221 = GM_LD(void *, (((uint8_t *)t220) + 0));
                void * t222 = (void *)(((uint8_t *)t221) + 8);
                gm_t88rcucg1b7uk t223 = gm_l19;
                uint8_t t224 = gm_f_is_error_8k27gd1(t222, GM_CONV(gm_csguankko4lmhd, t223));
                uint8_t t225 = (uint8_t)!t224;
                if (!t225) goto ie213;
                {
                    void * t227 = gm_l0;
                    gm_tl874t2atnlh3 t228 = gm_l3;
                    void * t229 = gm_l0;
                    gm_t88rcucg1b7uk t230 = gm_l19;
                    gm_b16_8 t231 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t229, t230);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t232;
                    GM_ST(gm_b16_8, ((uint8_t *)&t232 + 0), t231);
                    gm_tabb710d9mq2e t233;
                    gm_f_format_mkrut0g((void *)&t233, (void *)NULL, cx, NULL, t232);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t234 = t233;
                    if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 8, gm_srcici0r1ok5poc, 197, 36);
                    gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&t234) + 8));
                    gm_b16_8 t236 = t235;
                    gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
                    gm_b0_1 t238 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t227, t228, t237);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix213;
ie213:;
ix213:;
ix200:;
            }
            {
                gm_t88rcucg1b7uk t239 = gm_l5;
                gm_teo9d4mncjfdu t240;
                memset((uint8_t *)&t240 + 12, 0, 4);
                gm_te9hrk0u35rev t241;
                GM_ST(uint8_t, (uint8_t *)&t241 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t241 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t241 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t241 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t241 + 0), ((uint8_t)1ULL));
                GM_ST(gm_te9hrk0u35rev, (uint8_t *)&t240 + 0, t241);
                void * t242 = gm_l20;
                GM_ST(void *, (uint8_t *)&t240 + 16, t242);
                gm_teti411eujvf4 t243;
                memset(&t243, 0, sizeof t243);
                GM_ST(uint8_t, ((uint8_t *)&t243 + 0), ((uint8_t)49ULL));
                GM_ST(gm_teo9d4mncjfdu, ((uint8_t *)&t243 + 8), t240);
                gm_tl874t2atnlh3 t244 = gm_l3;
                void * t245 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t239, &t243, t244);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t246 = 0;
                F.ret = t245;
                goto gm_fret;
            }
            goto ix197;
ie197:;
ix197:;
        }
        {
            void * t247 = gm_l0;
            void * t248 = gm_l2;
            gm_tkn42u7na3t81 t249 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l18) + 20));
            void * t250 = gm_f_break_value_nkons87((void *)NULL, cx, NULL, t247, t248, t249);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t250;
        }
        {
            gm_t88rcucg1b7uk t251;
            void * t253 = gm_l22;
            void * t254 = t253;
            if (!(GM_LD(void *, ((uint8_t *)&t254)) != 0)) goto ie252;
            void * t255 = GM_LD(void *, ((uint8_t *)&t254));
            gm_l23 = t255;
            gm_t88rcucg1b7uk t257;
            if (!(((((uint8_t *)&F.l18) + 20))[4] != 0)) goto ie258;
            gm_t88rcucg1b7uk t259 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l18) + 20));
            gm_l24 = t259;
            gm_t88rcucg1b7uk t261 = gm_l24;
            gm_t88rcucg1b7uk t262 = t261;
            t257 = t262;
            goto ix258;
ie258:;
            void * t264 = gm_l23;
            gm_t88rcucg1b7uk t265 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t264) + 0));
            gm_t88rcucg1b7uk t266 = t265;
            t257 = t266;
ix258:;
            gm_t88rcucg1b7uk t267 = t257;
            t251 = t267;
            goto ix252;
ie252:;
            void * t269 = gm_l0;
            void * t270 = GM_LD(void *, (((uint8_t *)t269) + 0));
            void * t271 = (void *)(((uint8_t *)t270) + 8);
            gm_t88rcucg1b7uk t272 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t271);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t273 = t272;
            t251 = t273;
ix252:;
            gm_l25 = t251;
        }
        {
            if (!(((((uint8_t *)&F.l18) + 4))[4] != 0)) goto ie274;
            uint32_t t275 = GM_LD(uint32_t, (((uint8_t *)&F.l18) + 4));
            gm_l26 = t275;
            {
                void * t277 = gm_l0;
                void * t278 = (void *)(((uint8_t *)t277) + 920);
                gm_taqu3436kubri t279;
                uint32_t t280 = gm_l26;
                GM_ST(uint32_t, (uint8_t *)&t279 + 0, t280);
                gm_t88rcucg1b7uk t281 = gm_l25;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t279 + 4, t281);
                gm_tl874t2atnlh3 t282 = gm_l3;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t279 + 8, t282);
                gm_tftb1odc739t5 t283 = gm_f_push_26bmlpd(t278, &t279);
                gm_tftb1odc739t5 t284 = t283;
                if (((((uint8_t *)&t284))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t284) + 1, gm_srcici0r1ok5poc, 203, 34);
                gm_b0_1 t285 = GM_LD(gm_b0_1, (((uint8_t *)&t284) + 1));
            }
            goto ix274;
ie274:;
ix274:;
        }
        {
            if (!(((((uint8_t *)&F.l18) + 12))[4] != 0)) goto ie286;
            uint32_t t287 = GM_LD(uint32_t, (((uint8_t *)&F.l18) + 12));
            gm_l27 = t287;
            {
                void * t289 = gm_l0;
                void * t290 = GM_LD(void *, (((uint8_t *)t289) + 0));
                void * t291 = (void *)(((uint8_t *)t290) + 720);
                gm_taqu3436kubri t292;
                uint32_t t293 = gm_l27;
                GM_ST(uint32_t, (uint8_t *)&t292 + 0, t293);
                void * t294 = gm_l0;
                void * t295 = (void *)(((uint8_t *)t294) + 96);
                void * t296 = gm_l0;
                void * t297 = GM_LD(void *, (((uint8_t *)t296) + 0));
                gm_t88rcucg1b7uk t298 = gm_l25;
                gm_t88rcucg1b7uk t299 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t295, t297, t298);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t292 + 4, t299);
                gm_tl874t2atnlh3 t300 = gm_l3;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t292 + 8, t300);
                gm_tftb1odc739t5 t301 = gm_f_push_gfi6h9h(t291, &t292);
                gm_tftb1odc739t5 t302 = t301;
                if (((((uint8_t *)&t302))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t302) + 1, gm_srcici0r1ok5poc, 204, 37);
                gm_b0_1 t303 = GM_LD(gm_b0_1, (((uint8_t *)&t302) + 1));
            }
            goto ix286;
ie286:;
ix286:;
        }
        {
            gm_t88rcucg1b7uk t304 = gm_l5;
            gm_teo9d4mncjfdu t305;
            memset((uint8_t *)&t305 + 12, 0, 4);
            gm_te9hrk0u35rev t306;
            GM_ST(uint8_t, (uint8_t *)&t306 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t306 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t306 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t306 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t306 + 0), ((uint8_t)1ULL));
            GM_ST(gm_te9hrk0u35rev, (uint8_t *)&t305 + 0, t306);
            void * t307 = gm_l22;
            GM_ST(void *, (uint8_t *)&t305 + 16, t307);
            gm_teti411eujvf4 t308;
            memset(&t308, 0, sizeof t308);
            GM_ST(uint8_t, ((uint8_t *)&t308 + 0), ((uint8_t)49ULL));
            GM_ST(gm_teo9d4mncjfdu, ((uint8_t *)&t308 + 8), t305);
            gm_tl874t2atnlh3 t309 = gm_l3;
            void * t310 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t304, &t308, t309);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t311 = 0;
            F.ret = t310;
            goto gm_fret;
        }
        goto me17;
mn184:;
ma20:;
        uint8_t t313 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t313 != ((uint8_t)2ULL)) goto mn312;
        void * t314 = gm_l0;
        void * t315 = GM_LD(void *, (((uint8_t *)t314) + 0));
        gm_tl874t2atnlh3 t316 = gm_l3;
        void * t317 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t315, t316);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t318 = 0;
        F.ret = t317;
        goto gm_fret;
        goto me17;
mn312:;
ma21:;
        gm_trap("no match arm matched the scrutinee", gm_srcici0r1ok5poc, 161, 5);
me17:;
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
inline __attribute__((aligned(4))) void * gm_f_break_value_nkons87(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2) {
    struct gm_fr_break_value_nkons87 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* value */
    gm_tkn42u7na3t81 gm_l2; /* expect */
    void * gm_l3; /* x */
    void * gm_l4; /* ex */
    gm_t88rcucg1b7uk gm_l5; /* t */
    GM_STACK_CHECK(&F, 167u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        void * t3 = t2;
        void * t4;
        if ((GM_LD(void *, ((uint8_t *)&t3)) != 0)) {
            void * t5 = GM_LD(void *, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            uint8_t t6 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l3 = t4;
    }
    {
        void * t7 = gm_l0;
        void * t8 = gm_l3;
        gm_tkn42u7na3t81 t9 = gm_l2;
        void * t10 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t7, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t10;
    }
    {
        if (!((((uint8_t *)&gm_l2))[4] != 0)) goto ie11;
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
        gm_l5 = t12;
        {
            void * t14 = gm_l0;
            void * t15 = gm_l4;
            gm_t88rcucg1b7uk t16 = gm_l5;
            void * t17 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t14, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        void * t19 = gm_l4;
        uint8_t t20 = 0;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_fn_clause_exit_5an7bnc(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_check_fn_clause_exit_5an7bnc F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* levels */
    gm_b16_8 gm_l2; /* what */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint8_t gm_l4; /* crossed */
    uint64_t gm_l6; /* n */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    GM_STACK_CHECK(&F, 636u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t3 = gm_l1;
        uint8_t t4 = ((t3 == ((uint32_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b0_1 t6;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        void * t9 = gm_l0;
        if (!(((((uint8_t *)t9) + 800))[112] != 0)) goto ie8;
        gm_tnqmlaad6uchl t10 = GM_LD(gm_tnqmlaad6uchl, (((uint8_t *)t9) + 800));
        F.l5 = t10;
        uint8_t t11 = ((GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0) + 0) == ((uint8_t)0ULL)));
        if (!t11) goto ie8;
        {
            gm_l4 = ((uint8_t)1);
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 576) + 0) + 8);
        gm_l6 = t14;
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint32_t t16 = gm_l1;
            uint64_t t17 = (uint64_t)t16;
            uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
            if (o19) gm_trap("integer overflow in `-`", gm_srcici0r1ok5poc, 46, 17);
            uint64_t t20 = r18;
            gm_l7 = t20;
        }
        {
            for (;;) {
                uint64_t t22 = gm_l8;
                uint64_t t23 = gm_l7;
                uint8_t t24 = (t22 < t23);
                if (!t24) goto lb21;
                {
                    uint64_t t27 = gm_l8;
                    uint64_t t28 = t27;
                    uint64_t t31 = GM_LD(uint64_t, ((uint8_t *)&t28));
                    gm_l9 = t31;
                    {
                        uint64_t t34 = gm_l9;
                        uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                        if (o36) gm_trap("integer overflow in `+`", gm_srcici0r1ok5poc, 47, 12);
                        uint64_t t37 = r35;
                        uint64_t t38 = gm_l6;
                        uint8_t t39 = (t37 > t38);
                        if (!t39) goto ie33;
                        {
                            goto lb21;
                        }
                        goto ix33;
ie33:;
ix33:;
                    }
                    {
                        uint64_t t42 = gm_l6;
                        uint64_t t43 = gm_l9;
                        uint64_t r44; bool o45 = __builtin_sub_overflow(t42, t43, &r44);
                        if (o45) gm_trap("integer overflow in `-`", gm_srcici0r1ok5poc, 48, 32);
                        uint64_t t46 = r44;
                        uint64_t r47; bool o48 = __builtin_sub_overflow(t46, ((uint64_t)1ULL), &r47);
                        if (o48) gm_trap("integer overflow in `-`", gm_srcici0r1ok5poc, 48, 32);
                        uint64_t t49 = r47;
                        uint64_t t50 = (uintptr_t)t49;
                        void * t51 = gm_l0;
                        uint64_t t52 = GM_LD(uintptr_t, ((((uint8_t *)t51) + 576) + 0) + 8);
                        if (t50 >= (uintptr_t)(t52)) gm_trap_index(t50, t52, gm_srcici0r1ok5poc, 48, 17);
                        if (!(((((uint8_t *)GM_LD(void *, ((((uint8_t *)t51) + 576) + 0)) + t50 * 384) + 184))[112] != 0)) goto ie41;
                        gm_tnqmlaad6uchl t53 = GM_LD(gm_tnqmlaad6uchl, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t51) + 576) + 0)) + t50 * 384) + 184));
                        F.l10 = t53;
                        uint8_t t54 = ((GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0) + 0) == ((uint8_t)0ULL)));
                        if (!t54) goto ie41;
                        {
                            gm_l4 = ((uint8_t)1);
                        }
                        goto ix41;
ie41:;
ix41:;
                    }
                    goto me29;
mn30:;
                    gm_trap("no match arm matched the scrutinee", gm_srcici0r1ok5poc, 46, 5);
me29:;
                }
lc21:;
                {
                    uint64_t t57 = gm_l8;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l8 = t60;
                }
            }
lb21:;
        }
    }
    {
        uint8_t t62 = gm_l4;
        if (!t62) goto ie61;
        {
            void * t64 = gm_l0;
            gm_tl874t2atnlh3 t65 = gm_l3;
            gm_b16_8 t66 = gm_l2;
            gm_tnpifv20irnph t67;
            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t66);
            gm_tabb710d9mq2e t68;
            gm_f_format_cgo7upp((void *)&t68, (void *)NULL, cx, NULL, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t69 = t68;
            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_srcici0r1ok5poc, 50, 33);
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
            gm_b16_8 t71 = t70;
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
            gm_b0_1 t73 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t64, t65, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix61;
ie61:;
ix61:;
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
inline __attribute__((aligned(4))) void * gm_f_continue_expr_53f04kf(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_continue_expr_53f04kf F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_t88rcucg1b7uk gm_l3; /* nv */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l6; /* levels */
    GM_STACK_CHECK(&F, 597u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l0;
        gm_tl874t2atnlh3 t7 = gm_l2;
        gm_tt5b9omhp87j8 t8;
        gm_f_find_jump_4qhhk7u((void *)&t8, (void *)NULL, cx, NULL, t6, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l1)), t7, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tt5b9omhp87j8 t9 = t8;
        if (!((((uint8_t *)&t9))[64] != 0)) goto mn11;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        uint32_t t13 = GM_LD(uint32_t, ((((uint8_t *)&t9) + 8) + 0));
        gm_l4 = t13;
        gm_t8enl5g7b22jb t14 = GM_LD(gm_t8enl5g7b22jb, ((((uint8_t *)&t9) + 8) + 8));
        F.l5 = t14;
        gm_t88rcucg1b7uk t15 = gm_l3;
        uint32_t t16 = gm_l4;
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 24));
        gm_tcc1rq4n1avb2 t18;
        GM_ST(uint32_t, ((uint8_t *)&t18 + 0), t16);
        GM_ST(uint32_t, ((uint8_t *)&t18 + 4), t17);
        gm_te9hrk0u35rev t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t19 + 4), t18);
        gm_teti411eujvf4 t20;
        memset(&t20, 0, sizeof t20);
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)50ULL));
        GM_ST(gm_te9hrk0u35rev, ((uint8_t *)&t20 + 8), t19);
        gm_tl874t2atnlh3 t21 = gm_l2;
        void * t22 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t15, &t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t23 = 0;
        F.ret = t22;
        goto gm_fret;
        goto me10;
mn11:;
        if (!((((uint8_t *)&t9))[64] != 0)) goto mn24;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t25 != ((uint8_t)2ULL)) goto mn24;
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&t9) + 8));
        gm_l6 = t26;
        gm_t88rcucg1b7uk t27 = gm_l3;
        uint32_t t28 = gm_l6;
        void * t29 = gm_l0;
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
        gm_tl874t2atnlh3 t31 = gm_l2;
        void * t32 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t30, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttggq7ng4kp3p t33;
        GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t33 + 0), t28);
        GM_ST(void *, ((uint8_t *)&t33 + 8), t32);
        gm_teti411eujvf4 t34;
        memset(&t34, 0, sizeof t34);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)51ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t34 + 8), t33);
        gm_tl874t2atnlh3 t35 = gm_l2;
        void * t36 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t27, &t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t37 = 0;
        F.ret = t36;
        goto gm_fret;
        goto me10;
mn24:;
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
        gm_tl874t2atnlh3 t41 = gm_l2;
        void * t42 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
        goto me10;
mn38:;
        gm_trap("no match arm matched the scrutinee", gm_srcici0r1ok5poc, 221, 5);
me10:;
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
__attribute__((aligned(4))) void gm_f_find_jump_4qhhk7u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2, uint8_t a3) {
    struct gm_fr_find_jump_4qhhk7u F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l2; /* span */
    uint8_t gm_l3; /* is_continue */
    uint64_t gm_l4; /* idx */
    gm_t08olq97rscn3 gm_l6; /* l */
    gm_t88rcucg1b7uk gm_l8; /* ret */
    gm_t08olq97rscn3 gm_l9; /* l */
    uint32_t gm_l11; /* levels */
    uint64_t gm_l12; /* k */
    gm_tuanp0jbrlb6j gm_l14; /* fl */
    gm_b16_8 gm_l15; /* n */
    void * gm_l16; /* f */
    uint64_t gm_l17; /* idx */
    GM_STACK_CHECK(&F, 4669u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 232) + 0));
        gm_b16_8 t5 = t4;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_tf5srh4mp4b3i t7 = gm_f_find_label_gtm0hva(t6, &(*(gm_t1s75i08qtptd *)((uint8_t *)a1)));
        gm_tf5srh4mp4b3i t8 = t7;
        if (!((((uint8_t *)&t8))[8] != 0)) goto ie2;
        uint64_t t9 = GM_LD(uint64_t, ((uint8_t *)&t8));
        gm_l4 = t9;
        {
            void * t12 = gm_l0;
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t12) + 520));
            if (!t13) goto ie11;
            uint64_t t14 = gm_l4;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 528));
            uint8_t t17 = (t14 < t16);
            if (!t17) goto ie11;
            {
                void * t19 = gm_l0;
                gm_tl874t2atnlh3 t20 = gm_l2;
                gm_b16_8 t21;
                GM_ST(void *, (uint8_t *)&t21, (void *)(gm_soc64bhm712u9));
                GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(64));
                gm_b0_1 t22 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t19, t20, t21);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix11;
ie11:;
ix11:;
        }
        {
            uint64_t t23 = gm_l4;
            uint64_t t24 = (uintptr_t)t23;
            void * t25 = gm_l0;
            uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 232) + 0) + 8);
            if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_srcici0r1ok5poc, 99, 17);
            gm_t8enl5g7b22jb t27 = GM_LD(gm_t8enl5g7b22jb, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t25) + 232) + 0)) + t24 * 48));
            F.l5 = t27;
        }
        {
            uint8_t t29 = gm_l3;
            if (!t29) goto ie28;
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 28));
            uint8_t t31 = (uint8_t)!t30;
            if (!t31) goto ie28;
            {
                void * t33 = gm_l0;
                gm_tl874t2atnlh3 t34 = gm_l2;
                gm_b16_8 t35;
                GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sqdjjj1odavib));
                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(33));
                gm_b0_1 t36 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t33, t34, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix28;
ie28:;
ix28:;
        }
        {
            gm_ti8jkg54tfeop t37;
            memset((uint8_t *)&t37 + 4, 0, 4);
            GM_ST(uint32_t, (uint8_t *)&t37 + 0, ((uint32_t)0ULL));
            memcpy((uint8_t *)&t37 + 8, ((uint8_t *)&F.l5), 48);
            gm_tl9o0tv3f6gtf t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
            GM_ST(gm_ti8jkg54tfeop, ((uint8_t *)&t38 + 8), t37);
            gm_tt5b9omhp87j8 t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 65, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 66, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 68, 0);
            GM_ST(gm_tl9o0tv3f6gtf, ((uint8_t *)&t39), t38);
            ((uint8_t *)&t39)[64] = 1;
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t42 = gm_l0;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)t42) + 520));
        if (!t43) goto ie41;
        {
            void * t45 = gm_l0;
            gm_tl874t2atnlh3 t46 = gm_l2;
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_soc64bhm712u9));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(64));
            gm_b0_1 t48 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t45, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tt5b9omhp87j8 t49;
            memset(&t49, 0, sizeof t49);
            uint8_t t50 = 0;
            F.ret = t49;
            goto gm_fret;
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        void * t51 = gm_l0;
        gm_t403eokj7ifg9 t52 = GM_LD(gm_t403eokj7ifg9, (((uint8_t *)t51) + 800));
        gm_t403eokj7ifg9 t53 = t52;
        gm_tnqmlaad6uchl t54;
        if (((((uint8_t *)&t53))[112] != 0)) {
            gm_tnqmlaad6uchl t55 = GM_LD(gm_tnqmlaad6uchl, (((uint8_t *)&t53) + 0));
            t54 = t55;
        } else {
            {
                if (!((((uint8_t *)a1))[32] != 0)) goto ie57;
                gm_t08olq97rscn3 t58 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a1));
                gm_l6 = t58;
                {
                    void * t60 = gm_l0;
                    gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l6) + 16));
                    gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
                    gm_tnpifv20irnph t63;
                    GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t62);
                    gm_tabb710d9mq2e t64;
                    gm_f_format_8ubh5ip((void *)&t64, (void *)NULL, cx, NULL, t63);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t65 = t64;
                    if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 8, gm_srcici0r1ok5poc, 110, 30);
                    gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 8));
                    gm_b16_8 t67 = t66;
                    gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                    gm_b0_1 t69 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t60, t61, t68);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix57;
ie57:;
                {
                    void * t71 = gm_l0;
                    gm_tl874t2atnlh3 t72 = gm_l2;
                    gm_b16_8 t73;
                    GM_ST(void *, (uint8_t *)&t73, (void *)(gm_sn4e8vibva5m7));
                    GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(44));
                    gm_b0_1 t74 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t71, t72, t73);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix57:;
            }
            gm_tt5b9omhp87j8 t75;
            memset(&t75, 0, sizeof t75);
            uint8_t t76 = 0;
            F.ret = t75;
            goto gm_fret;
        }
        F.l7 = t54;
    }
    {
        gm_t1s75i08qtptd t77 = GM_LD(gm_t1s75i08qtptd, ((uint8_t *)a1));
        gm_t1s75i08qtptd t78 = t77;
        gm_t08olq97rscn3 t79;
        if (((((uint8_t *)&t78))[32] != 0)) {
            gm_t08olq97rscn3 t80 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t78) + 0));
            t79 = t80;
        } else {
            {
                uint8_t t83 = ((GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0) + 0) == ((uint8_t)0ULL)));
                if (!t83) goto ie82;
                {
                    void * t85 = gm_l0;
                    gm_tl874t2atnlh3 t86 = gm_l2;
                    gm_b16_8 t87;
                    GM_ST(void *, (uint8_t *)&t87, (void *)(gm_srjh1vh9n4las));
                    GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(86));
                    gm_b0_1 t88 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t85, t86, t87);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_tt5b9omhp87j8 t89;
                    memset(&t89, 0, sizeof t89);
                    uint8_t t90 = 0;
                    F.ret = t89;
                    goto gm_fret;
                }
                goto ix82;
ie82:;
ix82:;
            }
            {
                uint8_t t92 = gm_l3;
                if (!t92) goto ie91;
                uint8_t t93 = ((GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0) + 0) == ((uint8_t)2ULL)));
                if (!t93) goto ie91;
                {
                    void * t95 = gm_l0;
                    gm_tl874t2atnlh3 t96 = gm_l2;
                    gm_b16_8 t97;
                    GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sa9mckopi2o5a));
                    GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(85));
                    gm_b0_1 t98 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t95, t96, t97);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_tt5b9omhp87j8 t99;
                    memset(&t99, 0, sizeof t99);
                    uint8_t t100 = 0;
                    F.ret = t99;
                    goto gm_fret;
                }
                goto ix91;
ie91:;
ix91:;
            }
            {
                void * t101 = gm_l0;
                void * t102 = (void *)(((uint8_t *)t101) + 96);
                void * t103 = gm_l0;
                void * t104 = GM_LD(void *, (((uint8_t *)t103) + 0));
                void * t105 = gm_l0;
                gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t105) + 168));
                gm_t88rcucg1b7uk t107 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t102, t104, t106);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t107;
            }
            {
                uint8_t t109 = gm_l3;
                if (!t109) goto ie108;
                void * t110 = gm_l0;
                void * t111 = GM_LD(void *, (((uint8_t *)t110) + 0));
                void * t112 = (void *)(((uint8_t *)t111) + 8);
                gm_t88rcucg1b7uk t113 = gm_l8;
                uint8_t t114 = gm_f_is_unit_n83ttnb(t112, GM_CONV(gm_csguankko4lmhd, t113));
                uint8_t t115 = (uint8_t)!t114;
                if (!t115) goto ie108;
                void * t116 = gm_l0;
                void * t117 = GM_LD(void *, (((uint8_t *)t116) + 0));
                void * t118 = (void *)(((uint8_t *)t117) + 8);
                gm_t88rcucg1b7uk t119 = gm_l8;
                uint8_t t120 = gm_f_is_error_8k27gd1(t118, GM_CONV(gm_csguankko4lmhd, t119));
                uint8_t t121 = (uint8_t)!t120;
                if (!t121) goto ie108;
                {
                    void * t123 = gm_l0;
                    gm_tl874t2atnlh3 t124 = gm_l2;
                    void * t125 = gm_l0;
                    gm_t88rcucg1b7uk t126 = gm_l8;
                    gm_b16_8 t127 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t125, t126);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t128;
                    GM_ST(gm_b16_8, ((uint8_t *)&t128 + 0), t127);
                    gm_tabb710d9mq2e t129;
                    gm_f_format_j8m87ti((void *)&t129, (void *)NULL, cx, NULL, t128);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t130 = t129;
                    if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 8, gm_srcici0r1ok5poc, 128, 28);
                    gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&t130) + 8));
                    gm_b16_8 t132 = t131;
                    gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&t132));
                    gm_b0_1 t134 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t123, t124, t133);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_tt5b9omhp87j8 t135;
                    memset(&t135, 0, sizeof t135);
                    uint8_t t136 = 0;
                    F.ret = t135;
                    goto gm_fret;
                }
                goto ix108;
ie108:;
ix108:;
            }
            gm_tt5b9omhp87j8 t137;
            uint8_t t139 = gm_l3;
            if (!t139) goto ie138;
            gm_tl9o0tv3f6gtf t141;
            memset(&t141, 0, sizeof t141);
            GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)2ULL));
            GM_ST(uint32_t, ((uint8_t *)&t141 + 8), ((uint32_t)0ULL));
            gm_tl9o0tv3f6gtf t142 = t141;
            gm_tt5b9omhp87j8 t143;
            GM_ST(uint8_t, (uint8_t *)&t143 + 65, 0);
            GM_ST(uint16_t, (uint8_t *)&t143 + 66, 0);
            GM_ST(uint32_t, (uint8_t *)&t143 + 68, 0);
            GM_ST(gm_tl9o0tv3f6gtf, ((uint8_t *)&t143), t142);
            ((uint8_t *)&t143)[64] = 1;
            t137 = t143;
            goto ix138;
ie138:;
            gm_tl9o0tv3f6gtf t145;
            memset(&t145, 0, sizeof t145);
            GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)1ULL));
            gm_tl9o0tv3f6gtf t146 = t145;
            gm_tt5b9omhp87j8 t147;
            GM_ST(uint8_t, (uint8_t *)&t147 + 65, 0);
            GM_ST(uint16_t, (uint8_t *)&t147 + 66, 0);
            GM_ST(uint32_t, (uint8_t *)&t147 + 68, 0);
            GM_ST(gm_tl9o0tv3f6gtf, ((uint8_t *)&t147), t146);
            ((uint8_t *)&t147)[64] = 1;
            t137 = t147;
ix138:;
            uint8_t t148 = 0;
            F.ret = t137;
            goto gm_fret;
        }
        gm_l9 = t79;
    }
    {
        gm_tnqmlaad6uchl t149 = F.l7;
        gm_t403eokj7ifg9 t150;
        GM_ST(uint8_t, (uint8_t *)&t150 + 113, 0);
        GM_ST(uint16_t, (uint8_t *)&t150 + 114, 0);
        GM_ST(uint32_t, (uint8_t *)&t150 + 116, 0);
        GM_ST(gm_tnqmlaad6uchl, ((uint8_t *)&t150), t149);
        ((uint8_t *)&t150)[112] = 1;
        F.l10 = t150;
    }
    {
        gm_l11 = ((uint32_t)0ULL);
    }
    {
        void * t151 = gm_l0;
        uint64_t t152 = GM_LD(uintptr_t, ((((uint8_t *)t151) + 576) + 0) + 8);
        gm_l12 = t152;
    }
    {
        for (;;) {
            uint64_t t154 = gm_l12;
            uint8_t t155 = (t154 > ((uint64_t)0ULL));
            if (!t155) goto lb153;
            {
                gm_t403eokj7ifg9 t158 = F.l10;
                gm_t403eokj7ifg9 t159 = t158;
                gm_tnqmlaad6uchl t160;
                if (((((uint8_t *)&t159))[112] != 0)) {
                    gm_tnqmlaad6uchl t161 = GM_LD(gm_tnqmlaad6uchl, (((uint8_t *)&t159) + 0));
                    t160 = t161;
                } else {
                    goto lb153;
                }
                F.l13 = t160;
            }
            {
                uint8_t t163 = gm_l3;
                if (!t163) goto ie162;
                if (!(((((uint8_t *)&F.l13) + 32))[32] != 0)) goto ie162;
                gm_tuanp0jbrlb6j t164 = GM_LD(gm_tuanp0jbrlb6j, (((uint8_t *)&F.l13) + 32));
                gm_l14 = t164;
                if (!(((((uint8_t *)&gm_l14) + 0))[16] != 0)) goto ie162;
                gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 0));
                gm_l15 = t165;
                gm_b16_8 t166 = gm_l15;
                gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                gm_b16_8 t168 = t166;
                gm_b16_8 t169 = t167;
                uint8_t t170 = (gm_eq0iqiubfm7e(((uint8_t *)&t168), ((uint8_t *)&t169)));
                if (!t170) goto ie162;
                {
                    void * t172 = gm_l0;
                    uint32_t t173 = gm_l11;
                    uint32_t r174; bool o175 = __builtin_add_overflow(t173, ((uint32_t)1ULL), &r174);
                    if (o175) gm_trap("integer overflow in `+`", gm_srcici0r1ok5poc, 140, 37);
                    uint32_t t176 = r174;
                    gm_b16_8 t177;
                    GM_ST(void *, (uint8_t *)&t177, (void *)(gm_sr31o7m0aohvd));
                    GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(8));
                    gm_tl874t2atnlh3 t178 = gm_l2;
                    gm_b0_1 t179 = gm_f_check_fn_clause_exit_5an7bnc((void *)NULL, cx, NULL, t172, t176, t177, t178);
                    if (gm_unw.kind) {
                        goto gm_ll156;
                    }
                }
                {
                    uint32_t t180 = gm_l11;
                    gm_tl9o0tv3f6gtf t181;
                    memset(&t181, 0, sizeof t181);
                    GM_ST(uint8_t, ((uint8_t *)&t181 + 0), ((uint8_t)2ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t181 + 8), t180);
                    gm_tt5b9omhp87j8 t182;
                    GM_ST(uint8_t, (uint8_t *)&t182 + 65, 0);
                    GM_ST(uint16_t, (uint8_t *)&t182 + 66, 0);
                    GM_ST(uint32_t, (uint8_t *)&t182 + 68, 0);
                    GM_ST(gm_tl9o0tv3f6gtf, ((uint8_t *)&t182), t181);
                    ((uint8_t *)&t182)[64] = 1;
                    uint8_t t183 = 0;
                    F.ret = t182;
                    goto gm_fret;
                }
                goto ix162;
ie162:;
ix162:;
            }
            {
                uint64_t t184 = gm_l12;
                uint64_t r185; bool o186 = __builtin_sub_overflow(t184, ((uint64_t)1ULL), &r185);
                if (o186) gm_trap("integer overflow in `-`", gm_srcici0r1ok5poc, 143, 9);
                uint64_t t187 = r185;
                gm_l12 = t187;
            }
            {
                uint64_t t188 = gm_l12;
                uint64_t t189 = (uintptr_t)t188;
                void * t190 = gm_l0;
                uint64_t t191 = GM_LD(uintptr_t, ((((uint8_t *)t190) + 576) + 0) + 8);
                if (t189 >= (uintptr_t)(t191)) gm_trap_index(t189, t191, gm_srcici0r1ok5poc, 144, 15);
                void * t192 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t190) + 576) + 0)) + t189 * 384);
                gm_l16 = t192;
            }
            {
                void * t194 = gm_l16;
                gm_b16_8 t195 = GM_LD(gm_b16_8, ((((uint8_t *)t194) + 104) + 0));
                gm_b16_8 t196 = t195;
                gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
                gm_tf5srh4mp4b3i t198 = gm_f_find_label_gtm0hva(t197, &(*(gm_t1s75i08qtptd *)((uint8_t *)a1)));
                gm_tf5srh4mp4b3i t199 = t198;
                if (!((((uint8_t *)&t199))[8] != 0)) goto ie193;
                uint64_t t200 = GM_LD(uint64_t, ((uint8_t *)&t199));
                gm_l17 = t200;
                {
                    uint64_t t202 = gm_l17;
                    uint64_t t203 = (uintptr_t)t202;
                    void * t204 = gm_l16;
                    uint64_t t205 = GM_LD(uintptr_t, ((((uint8_t *)t204) + 104) + 0) + 8);
                    if (t203 >= (uintptr_t)(t205)) gm_trap_index(t203, t205, gm_srcici0r1ok5poc, 146, 21);
                    gm_t8enl5g7b22jb t206 = GM_LD(gm_t8enl5g7b22jb, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t204) + 104) + 0)) + t203 * 48));
                    F.l18 = t206;
                }
                {
                    uint8_t t208 = gm_l3;
                    if (!t208) goto ie207;
                    uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 28));
                    uint8_t t210 = (uint8_t)!t209;
                    if (!t210) goto ie207;
                    {
                        void * t212 = gm_l0;
                        gm_tl874t2atnlh3 t213 = gm_l2;
                        gm_b16_8 t214;
                        GM_ST(void *, (uint8_t *)&t214, (void *)(gm_sqdjjj1odavib));
                        GM_ST(uintptr_t, (uint8_t *)&t214 + 8, (uintptr_t)(33));
                        gm_b0_1 t215 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t212, t213, t214);
                        if (gm_unw.kind) {
                            goto gm_ll156;
                        }
                    }
                    goto ix207;
ie207:;
ix207:;
                }
                {
                    void * t216 = gm_l0;
                    uint32_t t217 = gm_l11;
                    uint32_t r218; bool o219 = __builtin_add_overflow(t217, ((uint32_t)1ULL), &r218);
                    if (o219) gm_trap("integer overflow in `+`", gm_srcici0r1ok5poc, 148, 37);
                    uint32_t t220 = r218;
                    gm_b16_8 t221;
                    uint8_t t223 = gm_l3;
                    if (!t223) goto ie222;
                    gm_b16_8 t225;
                    GM_ST(void *, (uint8_t *)&t225, (void *)(gm_sr31o7m0aohvd));
                    GM_ST(uintptr_t, (uint8_t *)&t225 + 8, (uintptr_t)(8));
                    gm_b16_8 t226 = t225;
                    t221 = t226;
                    goto ix222;
ie222:;
                    gm_b16_8 t228;
                    GM_ST(void *, (uint8_t *)&t228, (void *)(gm_s8av3ru5o85v4));
                    GM_ST(uintptr_t, (uint8_t *)&t228 + 8, (uintptr_t)(5));
                    gm_b16_8 t229 = t228;
                    t221 = t229;
ix222:;
                    gm_tl874t2atnlh3 t230 = gm_l2;
                    gm_b0_1 t231 = gm_f_check_fn_clause_exit_5an7bnc((void *)NULL, cx, NULL, t216, t220, t221, t230);
                    if (gm_unw.kind) {
                        goto gm_ll156;
                    }
                }
                {
                    gm_ti8jkg54tfeop t232;
                    memset((uint8_t *)&t232 + 4, 0, 4);
                    uint32_t t233 = gm_l11;
                    uint32_t r234; bool o235 = __builtin_add_overflow(t233, ((uint32_t)1ULL), &r234);
                    if (o235) gm_trap("integer overflow in `+`", gm_srcici0r1ok5poc, 149, 35);
                    uint32_t t236 = r234;
                    GM_ST(uint32_t, (uint8_t *)&t232 + 0, t236);
                    memcpy((uint8_t *)&t232 + 8, ((uint8_t *)&F.l18), 48);
                    gm_tl9o0tv3f6gtf t237;
                    GM_ST(uint8_t, (uint8_t *)&t237 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t237 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t237 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t237 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_ti8jkg54tfeop, ((uint8_t *)&t237 + 8), t232);
                    gm_tt5b9omhp87j8 t238;
                    GM_ST(uint8_t, (uint8_t *)&t238 + 65, 0);
                    GM_ST(uint16_t, (uint8_t *)&t238 + 66, 0);
                    GM_ST(uint32_t, (uint8_t *)&t238 + 68, 0);
                    GM_ST(gm_tl9o0tv3f6gtf, ((uint8_t *)&t238), t237);
                    ((uint8_t *)&t238)[64] = 1;
                    uint8_t t239 = 0;
                    F.ret = t238;
                    goto gm_fret;
                }
                goto ix193;
ie193:;
ix193:;
            }
            {
                void * t240 = gm_l16;
                gm_t403eokj7ifg9 t241 = GM_LD(gm_t403eokj7ifg9, (((uint8_t *)t240) + 184));
                F.l10 = t241;
            }
            {
                uint32_t t242 = gm_l11;
                uint32_t r243; bool o244 = __builtin_add_overflow(t242, ((uint32_t)1ULL), &r243);
                if (o244) gm_trap("integer overflow in `+`", gm_srcici0r1ok5poc, 152, 9);
                uint32_t t245 = r243;
                gm_l11 = t245;
            }
lc153:;
        }
gm_ll156:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb153; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc153; }
        }
        goto gm_lad;
lb153:;
    }
    {
        void * t246 = gm_l0;
        gm_tl874t2atnlh3 t247 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l9) + 16));
        gm_b16_8 t248 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
        gm_tnpifv20irnph t249;
        GM_ST(gm_b16_8, ((uint8_t *)&t249 + 0), t248);
        gm_tabb710d9mq2e t250;
        gm_f_format_8ubh5ip((void *)&t250, (void *)NULL, cx, NULL, t249);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t251 = t250;
        if (((((uint8_t *)&t251))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t251) + 8, gm_srcici0r1ok5poc, 154, 22);
        gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)&t251) + 8));
        gm_b16_8 t253 = t252;
        gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
        gm_b0_1 t255 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t246, t247, t254);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tt5b9omhp87j8 t256;
        memset(&t256, 0, sizeof t256);
        uint8_t t257 = 0;
        F.ret = t256;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_label_gtm0hva(gm_b16_8 a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_label_gtm0hva F;
    gm_b16_8 gm_l0; /* labels */
    uint64_t gm_l2; /* i */
    gm_t08olq97rscn3 gm_l4; /* l */
    uint8_t gm_l5; /* found */
    GM_STACK_CHECK_SHALLOW(&F, 386u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l2 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l2;
            uint8_t t5 = (t4 > ((uint64_t)0ULL));
            if (!t5) goto lb3;
            {
                uint64_t t8 = gm_l2;
                uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
                if (o10) gm_trap("integer overflow in `-`", gm_srcici0r1ok5poc, 87, 9);
                uint64_t t11 = r9;
                gm_l2 = t11;
            }
            {
                uint64_t t12 = gm_l2;
                uint64_t t13 = (uintptr_t)t12;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t13 >= (uintptr_t)(t14)) gm_trap_index(t13, t14, gm_srcici0r1ok5poc, 88, 14);
                gm_t8enl5g7b22jb t15 = GM_LD(gm_t8enl5g7b22jb, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t13 * 48));
                F.l3 = t15;
            }
            {
                uint8_t t16;
                if (!((((uint8_t *)a1))[32] != 0)) goto ie17;
                gm_t08olq97rscn3 t18 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a1));
                gm_l4 = t18;
                gm_th2lqbo77pvsc t20 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l3) + 0));
                gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                gm_th2lqbo77pvsc t22;
                GM_ST(uint8_t, (uint8_t *)&t22 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t22 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t22 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t22), t21);
                ((uint8_t *)&t22)[16] = 1;
                gm_th2lqbo77pvsc t23 = t20;
                gm_th2lqbo77pvsc t24 = t22;
                uint8_t t25 = (gm_eq0ogca82g1f(((uint8_t *)&t23), ((uint8_t *)&t24)));
                uint8_t t26 = t25;
                t16 = t26;
                goto ix17;
ie17:;
                uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 28));
                uint8_t t29 = t28;
                t16 = t29;
ix17:;
                gm_l5 = t16;
            }
            {
                uint8_t t31 = gm_l5;
                if (!t31) goto ie30;
                {
                    uint64_t t33 = gm_l2;
                    gm_tf5srh4mp4b3i t34;
                    GM_ST(uint8_t, (uint8_t *)&t34 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t34 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t34 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t34), t33);
                    ((uint8_t *)&t34)[8] = 1;
                    uint8_t t35 = 0;
                    F.ret = t34;
                    goto gm_fret;
                }
                goto ix30;
ie30:;
ix30:;
            }
lc3:;
        }
lb3:;
    }
    {
        gm_tf5srh4mp4b3i t36;
        memset(&t36, 0, sizeof t36);
        uint8_t t37 = 0;
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_label_gtm0hva_c(gm_b16_8 a0, gm_t1s75i08qtptd a1) { return gm_f_find_label_gtm0hva(a0, &a1); }
__attribute__((aligned(4))) void * gm_f_open_return_5q4g4bo(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_open_return_5q4g4bo F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* tys */
    void * gm_l4; /* value */
    void * gm_l5; /* x */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 836u);
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
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        gm_tl874t2atnlh3 t7 = gm_l2;
        void * t8 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t8;
    }
    {
        void * t10 = gm_l1;
        void * t11 = t10;
        if (!(GM_LD(void *, ((uint8_t *)&t11)) != 0)) goto ie9;
        void * t12 = GM_LD(void *, ((uint8_t *)&t11));
        gm_l5 = t12;
        {
            void * t15 = gm_l0;
            if (!(((((uint8_t *)t15) + 748))[4] != 0)) goto ie14;
            gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t15) + 748));
            gm_l6 = t16;
            {
                void * t18 = gm_l0;
                void * t19 = gm_l0;
                void * t20 = gm_l5;
                gm_t88rcucg1b7uk t21 = gm_l6;
                gm_tkn42u7na3t81 t22;
                GM_ST(uint8_t, (uint8_t *)&t22 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t22 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t22), t21);
                ((uint8_t *)&t22)[4] = 1;
                void * t23 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t19, t20, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t24 = gm_l6;
                void * t25 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t18, t23, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t25;
            }
            goto ix14;
ie14:;
            {
                void * t27 = gm_l0;
                void * t28 = gm_l5;
                gm_tkn42u7na3t81 t29;
                memset(&t29, 0, sizeof t29);
                void * t30 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t27, t28, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t30;
            }
            {
                void * t32 = gm_l3;
                void * t33 = gm_l4;
                gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t33) + 0));
                gm_t53at80pfdus3 t35;
                gm_f_kind_jcnt4hm((void *)&t35, t32, GM_CONV(gm_csguankko4lmhd, t34));
                gm_t53at80pfdus3 t36 = t35;
                uint8_t t37 = (!(GM_LD(uint8_t, ((uint8_t *)&t36) + 0) == ((uint8_t)5ULL)));
                if (!t37) goto ie31;
                void * t38 = gm_l3;
                void * t39 = gm_l4;
                gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t39) + 0));
                uint8_t t41 = gm_f_is_error_8k27gd1(t38, GM_CONV(gm_csguankko4lmhd, t40));
                uint8_t t42 = (uint8_t)!t41;
                if (!t42) goto ie31;
                {
                    void * t44 = gm_l0;
                    void * t45 = gm_l4;
                    gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t45) + 0));
                    gm_tkn42u7na3t81 t47;
                    GM_ST(uint8_t, (uint8_t *)&t47 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t47 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t47), t46);
                    ((uint8_t *)&t47)[4] = 1;
                    GM_ST(gm_tkn42u7na3t81, (((uint8_t *)t44) + 748), t47);
                }
                goto ix31;
ie31:;
ix31:;
            }
ix14:;
        }
        goto ix9;
ie9:;
        void * t49 = gm_l0;
        if (!(((((uint8_t *)t49) + 748))[4] != 0)) goto ie48;
        gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t49) + 748));
        gm_l7 = t50;
        {
            void * t53 = gm_l3;
            void * t54 = gm_l0;
            void * t55 = (void *)(((uint8_t *)t54) + 96);
            void * t56 = gm_l0;
            void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
            gm_t88rcucg1b7uk t58 = gm_l7;
            gm_t88rcucg1b7uk t59 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t55, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t60 = gm_f_is_unit_n83ttnb(t53, GM_CONV(gm_csguankko4lmhd, t59));
            uint8_t t61 = (uint8_t)!t60;
            if (!t61) goto ie52;
            void * t62 = gm_l3;
            gm_t88rcucg1b7uk t63 = gm_l7;
            uint8_t t64 = gm_f_is_error_8k27gd1(t62, GM_CONV(gm_csguankko4lmhd, t63));
            uint8_t t65 = (uint8_t)!t64;
            if (!t65) goto ie52;
            {
                void * t67 = gm_l0;
                gm_tl874t2atnlh3 t68 = gm_l2;
                void * t69 = gm_l0;
                gm_t88rcucg1b7uk t70 = gm_l7;
                gm_b16_8 t71 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t69, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t72;
                GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t71);
                gm_tabb710d9mq2e t73;
                gm_f_format_hqail32((void *)&t73, (void *)NULL, cx, NULL, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t74 = t73;
                if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 8, gm_srcici0r1ok5poc, 66, 96);
                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 8));
                gm_b16_8 t76 = t75;
                gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                gm_b0_1 t78 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t67, t68, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix52;
ie52:;
ix52:;
        }
        goto ix48;
ie48:;
        {
            void * t80 = gm_l0;
            uint8_t *t81 = (((uint8_t *)t80) + 748);
            void * t82 = gm_l3;
            gm_t88rcucg1b7uk t83 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t84;
            GM_ST(uint8_t, (uint8_t *)&t84 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t84 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t84), t83);
            ((uint8_t *)&t84)[4] = 1;
            GM_ST(gm_tkn42u7na3t81, t81, t84);
        }
ix48:;
ix9:;
    }
    {
        void * t85 = gm_l3;
        gm_t88rcucg1b7uk t86 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t87 = gm_l4;
        gm_ttggq7ng4kp3p t88;
        GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t88 + 0), ((uint32_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t88 + 8), t87);
        gm_teti411eujvf4 t89;
        memset(&t89, 0, sizeof t89);
        GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)51ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t89 + 8), t88);
        gm_tl874t2atnlh3 t90 = gm_l2;
        void * t91 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t86, &t89, t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t92 = 0;
        F.ret = t91;
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
__attribute__((aligned(4))) void * gm_f_return_expr_0qs023i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_return_expr_0qs023i F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_tplpbqf3duvn3 gm_l3; /* target */
    uint32_t gm_l4; /* levels */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    void * gm_l6; /* x */
    void * gm_l7; /* value */
    GM_STACK_CHECK(&F, 848u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 520));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            gm_tl874t2atnlh3 t7 = gm_l2;
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_svn0tfk5ehe5n));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(47));
            gm_b0_1 t9 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t6, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        gm_tplpbqf3duvn3 t11 = gm_f_return_target_ijqt4mp(t10);
        gm_l3 = t11;
    }
    {
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
        gm_l4 = t12;
    }
    {
        void * t13 = gm_l0;
        uint32_t t14 = gm_l4;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s277jq8ulrihr));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(6));
        gm_tl874t2atnlh3 t16 = gm_l2;
        gm_b0_1 t17 = gm_f_check_fn_clause_exit_5an7bnc((void *)NULL, cx, NULL, t13, t14, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t19 = gm_l4;
        uint8_t t20 = ((t19 == ((uint32_t)0ULL)));
        if (!t20) goto ie18;
        void * t21 = gm_l0;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)t21) + 744));
        if (!t22) goto ie18;
        {
            void * t24 = gm_l0;
            void * t25 = gm_l1;
            gm_tl874t2atnlh3 t26 = gm_l2;
            void * t27 = gm_f_open_return_5q4g4bo((void *)NULL, cx, NULL, t24, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        uint32_t t30 = gm_l4;
        uint8_t t31 = (t30 > ((uint32_t)0ULL));
        if (!t31) goto ie29;
        void * t32 = gm_l0;
        uint64_t t33 = GM_LD(uintptr_t, ((((uint8_t *)t32) + 576) + 0) + 8);
        uint32_t t34 = gm_l4;
        uint64_t t35 = (uint64_t)t34;
        uint64_t r36; bool o37 = __builtin_sub_overflow(t33, t35, &r36);
        if (o37) gm_trap("integer overflow in `-`", gm_srcici0r1ok5poc, 9, 38);
        uint64_t t38 = r36;
        uint64_t t39 = (uintptr_t)t38;
        void * t40 = gm_l0;
        uint64_t t41 = GM_LD(uintptr_t, ((((uint8_t *)t40) + 576) + 0) + 8);
        if (t39 >= (uintptr_t)(t41)) gm_trap_index(t39, t41, gm_srcici0r1ok5poc, 9, 23);
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t40) + 576) + 0)) + t39 * 384) + 169));
        if (!t42) goto ie29;
        {
            void * t44 = gm_l0;
            gm_tl874t2atnlh3 t45 = gm_l2;
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_ska00mcp5pgpq));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(52));
            void * t47 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t44, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t48 = 0;
            F.ret = t47;
            goto gm_fret;
        }
        goto ix29;
ie29:;
ix29:;
    }
    {
        gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 4));
        gm_l5 = t49;
    }
    {
        void * t50;
        void * t52 = gm_l1;
        void * t53 = t52;
        if (!(GM_LD(void *, ((uint8_t *)&t53)) != 0)) goto ie51;
        void * t54 = GM_LD(void *, ((uint8_t *)&t53));
        gm_l6 = t54;
        void * t56 = gm_l0;
        void * t57 = gm_l0;
        void * t58 = gm_l6;
        gm_t88rcucg1b7uk t59 = gm_l5;
        gm_tkn42u7na3t81 t60;
        GM_ST(uint8_t, (uint8_t *)&t60 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t60 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t60), t59);
        ((uint8_t *)&t60)[4] = 1;
        void * t61 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t57, t58, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t62 = gm_l5;
        void * t63 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t56, t61, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t64 = t63;
        t50 = t64;
        goto ix51;
ie51:;
        {
            void * t67 = gm_l0;
            void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
            void * t69 = (void *)(((uint8_t *)t68) + 8);
            gm_t88rcucg1b7uk t70 = gm_l5;
            uint8_t t71 = gm_f_is_unit_n83ttnb(t69, GM_CONV(gm_csguankko4lmhd, t70));
            uint8_t t72 = (uint8_t)!t71;
            if (!t72) goto ie66;
            void * t73 = gm_l0;
            void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
            void * t75 = (void *)(((uint8_t *)t74) + 8);
            gm_t88rcucg1b7uk t76 = gm_l5;
            uint8_t t77 = gm_f_is_error_8k27gd1(t75, GM_CONV(gm_csguankko4lmhd, t76));
            uint8_t t78 = (uint8_t)!t77;
            if (!t78) goto ie66;
            {
                void * t80 = gm_l0;
                gm_tl874t2atnlh3 t81 = gm_l2;
                void * t82 = gm_l0;
                gm_t88rcucg1b7uk t83 = gm_l5;
                gm_b16_8 t84 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t82, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t85;
                GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t84);
                gm_tabb710d9mq2e t86;
                gm_f_format_hqail32((void *)&t86, (void *)NULL, cx, NULL, t85);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t87 = t86;
                if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 8, gm_srcici0r1ok5poc, 17, 28);
                gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&t87) + 8));
                gm_b16_8 t89 = t88;
                gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                gm_b0_1 t91 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t80, t81, t90);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix66;
ie66:;
ix66:;
        }
        void * t92 = gm_l0;
        void * t93 = GM_LD(void *, (((uint8_t *)t92) + 0));
        gm_tl874t2atnlh3 t94 = gm_l2;
        void * t95 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t93, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t96 = t95;
        t50 = t96;
ix51:;
        gm_l7 = t50;
    }
    {
        void * t97 = gm_l0;
        void * t98 = GM_LD(void *, (((uint8_t *)t97) + 0));
        void * t99 = (void *)(((uint8_t *)t98) + 8);
        gm_t88rcucg1b7uk t100 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t99);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t101 = gm_l4;
        void * t102 = gm_l7;
        gm_ttggq7ng4kp3p t103;
        GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t103 + 0), t101);
        GM_ST(void *, ((uint8_t *)&t103 + 8), t102);
        gm_teti411eujvf4 t104;
        memset(&t104, 0, sizeof t104);
        GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)51ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t104 + 8), t103);
        gm_tl874t2atnlh3 t105 = gm_l2;
        void * t106 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t100, &t104, t105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t107 = 0;
        F.ret = t106;
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
inline __attribute__((aligned(4))) gm_tplpbqf3duvn3 gm_f_return_target_ijqt4mp(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_return_target_ijqt4mp F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* levels */
    uint64_t gm_l2; /* k */
    void * gm_l3; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 218u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4;
        if ((((((uint8_t *)t3) + 800))[112] != 0)) goto mn6;
        t4 = ((uint8_t)1);
        goto me5;
mn6:;
        t4 = ((uint8_t)0);
        goto me5;
mn7:;
        gm_trap("no match arm matched the scrutinee", gm_srcici0r1ok5poc, 27, 8);
me5:;
        if (!t4) goto ie2;
        {
            void * t9 = gm_l0;
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 168));
            gm_tplpbqf3duvn3 t11;
            GM_ST(uint32_t, ((uint8_t *)&t11 + 0), ((uint32_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t11 + 4), t10);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_l1 = ((uint32_t)0ULL);
    }
    {
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 576) + 0) + 8);
        gm_l2 = t14;
    }
    {
        for (;;) {
            uint64_t t16 = gm_l2;
            uint8_t t17 = (t16 > ((uint64_t)0ULL));
            if (!t17) goto lb15;
            {
                uint64_t t20 = gm_l2;
                uint64_t r21; bool o22 = __builtin_sub_overflow(t20, ((uint64_t)1ULL), &r21);
                if (o22) gm_trap("integer overflow in `-`", gm_srcici0r1ok5poc, 31, 9);
                uint64_t t23 = r21;
                gm_l2 = t23;
            }
            {
                uint32_t t24 = gm_l1;
                uint32_t r25; bool o26 = __builtin_add_overflow(t24, ((uint32_t)1ULL), &r25);
                if (o26) gm_trap("integer overflow in `+`", gm_srcici0r1ok5poc, 32, 9);
                uint32_t t27 = r25;
                gm_l1 = t27;
            }
            {
                uint64_t t28 = gm_l2;
                uint64_t t29 = (uintptr_t)t28;
                void * t30 = gm_l0;
                uint64_t t31 = GM_LD(uintptr_t, ((((uint8_t *)t30) + 576) + 0) + 8);
                if (t29 >= (uintptr_t)(t31)) gm_trap_index(t29, t31, gm_srcici0r1ok5poc, 33, 15);
                void * t32 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t30) + 576) + 0)) + t29 * 384);
                gm_l3 = t32;
            }
            {
                void * t34 = gm_l3;
                uint8_t t35;
                if ((((((uint8_t *)t34) + 184))[112] != 0)) goto mn37;
                t35 = ((uint8_t)1);
                goto me36;
mn37:;
                t35 = ((uint8_t)0);
                goto me36;
mn38:;
                gm_trap("no match arm matched the scrutinee", gm_srcici0r1ok5poc, 34, 12);
me36:;
                if (!t35) goto ie33;
                {
                    uint32_t t40 = gm_l1;
                    void * t41 = gm_l3;
                    gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t41) + 40));
                    gm_tplpbqf3duvn3 t43;
                    GM_ST(uint32_t, ((uint8_t *)&t43 + 0), t40);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t43 + 4), t42);
                    uint8_t t44 = 0;
                    F.ret = t43;
                    goto gm_fret;
                }
                goto ix33;
ie33:;
ix33:;
            }
lc15:;
        }
lb15:;
    }
    {
        void * t45 = gm_l0;
        gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t45) + 168));
        gm_tplpbqf3duvn3 t47;
        GM_ST(uint32_t, ((uint8_t *)&t47 + 0), ((uint32_t)0ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t47 + 4), t46);
        uint8_t t48 = 0;
        F.ret = t47;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
