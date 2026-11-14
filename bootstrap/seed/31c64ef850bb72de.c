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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint32_t a[1]; } gm_tdvq40fmif5j0;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint64_t f0; uint64_t f1; uint32_t f2; uint8_t p3[4]; uint64_t f3; } gm_tnen6863q2sjs;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_tcc1rq4n1avb2 f0; uint8_t f1; uint8_t pe[3]; } gm_t736djc66po0l;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
_Static_assert(sizeof(gm_tdvq40fmif5j0) == 4 && _Alignof(gm_tdvq40fmif5j0) == 4 && offsetof(gm_tdvq40fmif5j0, a) == 0, "layout of `[1]u32`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint32_t f0; uint32_t f1; gm_t9805qtonbsag f2; } gm_tf9f5m02f56gm;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[5]; gm_b16_8 f3; } gm_tftmsj5vtqvej;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_t9805qtonbsag f0; uint32_t f1; } gm_tgqgdb97ka5kt;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_ti71ll5knnvij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; } gm_tk5njge13tk1d;
typedef struct { gm_t9805qtonbsag f0; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_t9805qtonbsag f3; } gm_tk8ebsa8coi4p;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; } gm_tmr5oep1578tb;
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
_Static_assert(sizeof(gm_tnen6863q2sjs) == 32 && _Alignof(gm_tnen6863q2sjs) == 8 && offsetof(gm_tnen6863q2sjs, f0) == 0 && offsetof(gm_tnen6863q2sjs, f1) == 8 && offsetof(gm_tnen6863q2sjs, f2) == 16 && offsetof(gm_tnen6863q2sjs, f3) == 24, "layout of `(usize, usize, u32, usize)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { uint64_t f0; gm_t9805qtonbsag f1; } gm_tp0pp2cmr6tvd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
typedef struct { gm_t9805qtonbsag a[3]; } gm_tv199o0mcsqd6;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { gm_b16_8 f0; gm_tk5njge13tk1d f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t0u6dopi4f21f;
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint64_t f0; gm_t6j6smj7qoctu f1; } gm_t1l3r16b8478v;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t0rfe1ou14h21 f0; void * f1; } gm_t4grvdhp6dkf1;
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t736djc66po0l) == 12 && _Alignof(gm_t736djc66po0l) == 4 && offsetof(gm_t736djc66po0l, f0) == 0 && offsetof(gm_t736djc66po0l, f1) == 8, "layout of `?(u32, u32)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t6j6smj7qoctu v1; } f1; } gm_t9t4u07j77scv;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_t4c7ipjj9s3fp f0; gm_trqfvmjtt4sij f1; uint8_t f2; uint8_t p3[7]; gm_trqfvmjtt4sij f3; gm_t4c7ipjj9s3fp f4; gm_t4c7ipjj9s3fp f5; } gm_tbpdf5ej15iof;
typedef struct { uint64_t f0; gm_tgqgdb97ka5kt f1; uint8_t pe[4]; } gm_tc42mm7h7sccr;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tf9f5m02f56gm) == 16 && _Alignof(gm_tf9f5m02f56gm) == 4 && offsetof(gm_tf9f5m02f56gm, f0) == 0 && offsetof(gm_tf9f5m02f56gm, f1) == 4 && offsetof(gm_tf9f5m02f56gm, f2) == 8, "layout of `SpecKey`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tftmsj5vtqvej) == 24 && _Alignof(gm_tftmsj5vtqvej) == 8 && offsetof(gm_tftmsj5vtqvej, f0) == 0 && offsetof(gm_tftmsj5vtqvej, f1) == 1 && offsetof(gm_tftmsj5vtqvej, f2) == 2 && offsetof(gm_tftmsj5vtqvej, f3) == 8, "layout of `Options`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tgqgdb97ka5kt) == 12 && _Alignof(gm_tgqgdb97ka5kt) == 4 && offsetof(gm_tgqgdb97ka5kt, f0) == 0 && offsetof(gm_tgqgdb97ka5kt, f1) == 8, "layout of `Owner`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_th2lqbo77pvsc f0; uint8_t f1; uint8_t pe[7]; } gm_ti5ramhglnvdt;
_Static_assert(sizeof(gm_ti71ll5knnvij) == 72 && _Alignof(gm_ti71ll5knnvij) == 8 && offsetof(gm_ti71ll5knnvij, f0) == 0 && offsetof(gm_ti71ll5knnvij, f1) == 16 && offsetof(gm_ti71ll5knnvij, f2) == 32 && offsetof(gm_ti71ll5knnvij, f3) == 40 && offsetof(gm_ti71ll5knnvij, f4) == 56, "layout of `(string, []u8, usize, []u8, string)`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tk5njge13tk1d) == 176 && _Alignof(gm_tk5njge13tk1d) == 8 && offsetof(gm_tk5njge13tk1d, f0) == 0 && offsetof(gm_tk5njge13tk1d, f1) == 16 && offsetof(gm_tk5njge13tk1d, f2) == 32 && offsetof(gm_tk5njge13tk1d, f3) == 48 && offsetof(gm_tk5njge13tk1d, f4) == 64 && offsetof(gm_tk5njge13tk1d, f5) == 80 && offsetof(gm_tk5njge13tk1d, f6) == 96 && offsetof(gm_tk5njge13tk1d, f7) == 112 && offsetof(gm_tk5njge13tk1d, f8) == 128 && offsetof(gm_tk5njge13tk1d, f9) == 144 && offsetof(gm_tk5njge13tk1d, f10) == 160, "layout of `Parts`");
_Static_assert(sizeof(gm_tk8ebsa8coi4p) == 32 && _Alignof(gm_tk8ebsa8coi4p) == 4 && offsetof(gm_tk8ebsa8coi4p, f0) == 0 && offsetof(gm_tk8ebsa8coi4p, f1) == 8 && offsetof(gm_tk8ebsa8coi4p, f2) == 16 && offsetof(gm_tk8ebsa8coi4p, f3) == 24, "layout of `Ambient`");
_Static_assert(sizeof(gm_tmr5oep1578tb) == 56 && _Alignof(gm_tmr5oep1578tb) == 8 && offsetof(gm_tmr5oep1578tb, f0) == 0 && offsetof(gm_tmr5oep1578tb, f1) == 16 && offsetof(gm_tmr5oep1578tb, f2) == 32 && offsetof(gm_tmr5oep1578tb, f3) == 40, "layout of `(string, []u8, usize, []u8)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tp0pp2cmr6tvd) == 16 && _Alignof(gm_tp0pp2cmr6tvd) == 8 && offsetof(gm_tp0pp2cmr6tvd, f0) == 0 && offsetof(gm_tp0pp2cmr6tvd, f1) == 8, "layout of `(i: usize, x: ?u32)`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t4c7ipjj9s3fp f0; } gm_ttiitul0263ld;
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
typedef struct { gm_t4c7ipjj9s3fp f0; uint8_t f1; uint8_t pe[7]; } gm_tuutj662vpgtg;
_Static_assert(sizeof(gm_tv199o0mcsqd6) == 24 && _Alignof(gm_tv199o0mcsqd6) == 4 && offsetof(gm_tv199o0mcsqd6, a) == 0, "layout of `[3]?u32`");
_Static_assert(sizeof(gm_t0u6dopi4f21f) == 216 && _Alignof(gm_t0u6dopi4f21f) == 8 && offsetof(gm_t0u6dopi4f21f, f0) == 0 && offsetof(gm_t0u6dopi4f21f, f1) == 16 && offsetof(gm_t0u6dopi4f21f, f2) == 192 && offsetof(gm_t0u6dopi4f21f, f3) == 208 && offsetof(gm_t0u6dopi4f21f, f4) == 209, "layout of `Generated`");
_Static_assert(sizeof(gm_t1l3r16b8478v) == 40 && _Alignof(gm_t1l3r16b8478v) == 8 && offsetof(gm_t1l3r16b8478v, f0) == 0 && offsetof(gm_t1l3r16b8478v, f1) == 8, "layout of `(i: usize, x: CSource)`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4grvdhp6dkf1) == 72 && _Alignof(gm_t4grvdhp6dkf1) == 8 && offsetof(gm_t4grvdhp6dkf1, f0) == 0 && offsetof(gm_t4grvdhp6dkf1, f1) == 64, "layout of `Layouts`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { uint64_t f0; gm_tc42mm7h7sccr f1; } gm_t5c6io15et1sc;
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9t4u07j77scv) == 40 && _Alignof(gm_t9t4u07j77scv) == 8 && offsetof(gm_t9t4u07j77scv, f0) == 0 && offsetof(gm_t9t4u07j77scv, f1) == 8, "layout of `Unsup!ConstsText`");
_Static_assert(sizeof(gm_tbpdf5ej15iof) == 256 && _Alignof(gm_tbpdf5ej15iof) == 8 && offsetof(gm_tbpdf5ej15iof, f0) == 0 && offsetof(gm_tbpdf5ej15iof, f1) == 56 && offsetof(gm_tbpdf5ej15iof, f2) == 96 && offsetof(gm_tbpdf5ej15iof, f3) == 104 && offsetof(gm_tbpdf5ej15iof, f4) == 144 && offsetof(gm_tbpdf5ej15iof, f5) == 200, "layout of `Views`");
_Static_assert(sizeof(gm_tc42mm7h7sccr) == 24 && _Alignof(gm_tc42mm7h7sccr) == 8 && offsetof(gm_tc42mm7h7sccr, f0) == 0 && offsetof(gm_tc42mm7h7sccr, f1) == 8, "layout of `OwnerMark`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_ttiitul0263ld f5; } gm_te68172gglous;
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t0u6dopi4f21f v1; } f1; } gm_tetdkgoreqmn1;
_Static_assert(sizeof(gm_ti5ramhglnvdt) == 32 && _Alignof(gm_ti5ramhglnvdt) == 8 && offsetof(gm_ti5ramhglnvdt, f0) == 0 && offsetof(gm_ti5ramhglnvdt, f1) == 24, "layout of `??string`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
typedef struct { void * f0; gm_t4grvdhp6dkf1 f1; gm_trqfvmjtt4sij f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_ttiitul0263ld f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; gm_t4c7ipjj9s3fp f9; gm_trqfvmjtt4sij f10; gm_t4c7ipjj9s3fp f11; gm_trqfvmjtt4sij f12; gm_t4c7ipjj9s3fp f13; gm_t4c7ipjj9s3fp f14; gm_trqfvmjtt4sij f15; gm_t4c7ipjj9s3fp f16; uint8_t f17; uint8_t p18[7]; gm_t4c7ipjj9s3fp f18; gm_tbpdf5ej15iof f19; } gm_trg1s7erfs461;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_ttiitul0263ld) == 56 && _Alignof(gm_ttiitul0263ld) == 8 && offsetof(gm_ttiitul0263ld, f0) == 0, "layout of `Namer`");
_Static_assert(sizeof(gm_tuutj662vpgtg) == 64 && _Alignof(gm_tuutj662vpgtg) == 8 && offsetof(gm_tuutj662vpgtg, f0) == 0 && offsetof(gm_tuutj662vpgtg, f1) == 56, "layout of `?HashMap((u32, u32), bool, null, null)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t5c6io15et1sc) == 32 && _Alignof(gm_t5c6io15et1sc) == 8 && offsetof(gm_t5c6io15et1sc, f0) == 0 && offsetof(gm_t5c6io15et1sc, f1) == 8, "layout of `(i: usize, x: OwnerMark)`");
_Static_assert(sizeof(gm_te68172gglous) == 304 && _Alignof(gm_te68172gglous) == 8 && offsetof(gm_te68172gglous, f0) == 0 && offsetof(gm_te68172gglous, f1) == 40 && offsetof(gm_te68172gglous, f2) == 80 && offsetof(gm_te68172gglous, f3) == 136 && offsetof(gm_te68172gglous, f4) == 192 && offsetof(gm_te68172gglous, f5) == 248, "layout of `Helpers`");
_Static_assert(sizeof(gm_tetdkgoreqmn1) == 224 && _Alignof(gm_tetdkgoreqmn1) == 8 && offsetof(gm_tetdkgoreqmn1, f0) == 0 && offsetof(gm_tetdkgoreqmn1, f1) == 8, "layout of `Unsup!Generated`");
typedef struct { void * f0; gm_trg1s7erfs461 f1; gm_tftmsj5vtqvej f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_ttiitul0263ld f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; gm_t4c7ipjj9s3fp f9; gm_t4c7ipjj9s3fp f10; gm_t4c7ipjj9s3fp f11; gm_trqfvmjtt4sij f12; gm_t4c7ipjj9s3fp f13; gm_trqfvmjtt4sij f14; gm_t4c7ipjj9s3fp f15; gm_trqfvmjtt4sij f16; gm_trqfvmjtt4sij f17; gm_trqfvmjtt4sij f18; gm_trqfvmjtt4sij f19; gm_te68172gglous f20; gm_trqfvmjtt4sij f21; gm_trqfvmjtt4sij f22; gm_trqfvmjtt4sij f23; gm_t4c7ipjj9s3fp f24; gm_t4c7ipjj9s3fp f25; gm_tqetrgo0q24b5 f26; gm_trqfvmjtt4sij f27; gm_trqfvmjtt4sij f28; gm_t4c7ipjj9s3fp f29; gm_t4c7ipjj9s3fp f30; gm_trqfvmjtt4sij f31; gm_t4c7ipjj9s3fp f32; gm_t4c7ipjj9s3fp f33; gm_tuutj662vpgtg f34; gm_trqfvmjtt4sij f35; gm_trqfvmjtt4sij f36; gm_trqfvmjtt4sij f37; gm_trqfvmjtt4sij f38; gm_trqfvmjtt4sij f39; gm_trqfvmjtt4sij f40; gm_trqfvmjtt4sij f41; gm_trqfvmjtt4sij f42; gm_t4c7ipjj9s3fp f43; gm_t4c7ipjj9s3fp f44; gm_trqfvmjtt4sij f45; gm_t4c7ipjj9s3fp f46; gm_trqfvmjtt4sij f47; gm_t4c7ipjj9s3fp f48; gm_b16_8 f49; } gm_tpdgmavtftccj;
_Static_assert(sizeof(gm_trg1s7erfs461) == 1128 && _Alignof(gm_trg1s7erfs461) == 8 && offsetof(gm_trg1s7erfs461, f0) == 0 && offsetof(gm_trg1s7erfs461, f1) == 8 && offsetof(gm_trg1s7erfs461, f2) == 80 && offsetof(gm_trg1s7erfs461, f3) == 120 && offsetof(gm_trg1s7erfs461, f4) == 176 && offsetof(gm_trg1s7erfs461, f5) == 232 && offsetof(gm_trg1s7erfs461, f6) == 288 && offsetof(gm_trg1s7erfs461, f7) == 328 && offsetof(gm_trg1s7erfs461, f8) == 368 && offsetof(gm_trg1s7erfs461, f9) == 408 && offsetof(gm_trg1s7erfs461, f10) == 464 && offsetof(gm_trg1s7erfs461, f11) == 504 && offsetof(gm_trg1s7erfs461, f12) == 560 && offsetof(gm_trg1s7erfs461, f13) == 600 && offsetof(gm_trg1s7erfs461, f14) == 656 && offsetof(gm_trg1s7erfs461, f15) == 712 && offsetof(gm_trg1s7erfs461, f16) == 752 && offsetof(gm_trg1s7erfs461, f17) == 808 && offsetof(gm_trg1s7erfs461, f18) == 816 && offsetof(gm_trg1s7erfs461, f19) == 872, "layout of `CTypes`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_tpdgmavtftccj) == 3600 && _Alignof(gm_tpdgmavtftccj) == 8 && offsetof(gm_tpdgmavtftccj, f0) == 0 && offsetof(gm_tpdgmavtftccj, f1) == 8 && offsetof(gm_tpdgmavtftccj, f2) == 1136 && offsetof(gm_tpdgmavtftccj, f3) == 1160 && offsetof(gm_tpdgmavtftccj, f4) == 1216 && offsetof(gm_tpdgmavtftccj, f5) == 1272 && offsetof(gm_tpdgmavtftccj, f6) == 1328 && offsetof(gm_tpdgmavtftccj, f7) == 1368 && offsetof(gm_tpdgmavtftccj, f8) == 1408 && offsetof(gm_tpdgmavtftccj, f9) == 1448 && offsetof(gm_tpdgmavtftccj, f10) == 1504 && offsetof(gm_tpdgmavtftccj, f11) == 1560 && offsetof(gm_tpdgmavtftccj, f12) == 1616 && offsetof(gm_tpdgmavtftccj, f13) == 1656 && offsetof(gm_tpdgmavtftccj, f14) == 1712 && offsetof(gm_tpdgmavtftccj, f15) == 1752 && offsetof(gm_tpdgmavtftccj, f16) == 1808 && offsetof(gm_tpdgmavtftccj, f17) == 1848 && offsetof(gm_tpdgmavtftccj, f18) == 1888 && offsetof(gm_tpdgmavtftccj, f19) == 1928 && offsetof(gm_tpdgmavtftccj, f20) == 1968 && offsetof(gm_tpdgmavtftccj, f21) == 2272 && offsetof(gm_tpdgmavtftccj, f22) == 2312 && offsetof(gm_tpdgmavtftccj, f23) == 2352 && offsetof(gm_tpdgmavtftccj, f24) == 2392 && offsetof(gm_tpdgmavtftccj, f25) == 2448 && offsetof(gm_tpdgmavtftccj, f26) == 2504 && offsetof(gm_tpdgmavtftccj, f27) == 2552 && offsetof(gm_tpdgmavtftccj, f28) == 2592 && offsetof(gm_tpdgmavtftccj, f29) == 2632 && offsetof(gm_tpdgmavtftccj, f30) == 2688 && offsetof(gm_tpdgmavtftccj, f31) == 2744 && offsetof(gm_tpdgmavtftccj, f32) == 2784 && offsetof(gm_tpdgmavtftccj, f33) == 2840 && offsetof(gm_tpdgmavtftccj, f34) == 2896 && offsetof(gm_tpdgmavtftccj, f35) == 2960 && offsetof(gm_tpdgmavtftccj, f36) == 3000 && offsetof(gm_tpdgmavtftccj, f37) == 3040 && offsetof(gm_tpdgmavtftccj, f38) == 3080 && offsetof(gm_tpdgmavtftccj, f39) == 3120 && offsetof(gm_tpdgmavtftccj, f40) == 3160 && offsetof(gm_tpdgmavtftccj, f41) == 3200 && offsetof(gm_tpdgmavtftccj, f42) == 3240 && offsetof(gm_tpdgmavtftccj, f43) == 3280 && offsetof(gm_tpdgmavtftccj, f44) == 3336 && offsetof(gm_tpdgmavtftccj, f45) == 3392 && offsetof(gm_tpdgmavtftccj, f46) == 3432 && offsetof(gm_tpdgmavtftccj, f47) == 3488 && offsetof(gm_tpdgmavtftccj, f48) == 3528 && offsetof(gm_tpdgmavtftccj, f49) == 3584, "layout of `Gen`");
struct gm_fr_addr_name_g5lrlt3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_adt_mpqv8qe {
    gm_frame h;
    void * ret;
};
struct gm_fr_any_child_ffc8h1k {
    gm_frame h;
    gm_closure l1; /* pred */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_by_ref_fi0o860 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_c_abi_type_nu2c2tm {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_callee_name_4k9m9s6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_clause_may_exit_7lc6flj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_clause_name_uk8dps6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_code_has_arak3mp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_content_name_83mn5uf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_conv_attr_m0b89pc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_decl_table_l7u1k0g {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* names */
    gm_trqfvmjtt4sij l9; /* ptys */
    gm_trqfvmjtt4sij l14; /* params */
    gm_t9bmh80edou9c l15; /* <temporary array> */
    gm_trqfvmjtt4sij l17; /* args */
    gm_tdmi4fk9nhqa7 l18; /* <temporary array> */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_descriptors_fnnm3h1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_drain_leoibma {
    gm_frame h;
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_effect_id_5588nqa {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_empty_slice_08mdrht {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_13d6rhn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_2ntv1ru {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_360e8h0 {
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
struct gm_fr_empty_slice_7hq6cu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_al38tsc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_asvj6q6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_e8istsh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ednoq9d {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ekr3omu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_gm6v230 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_h06rie2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jagk2mc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jckt05t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jjmeosp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kge4ibg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_mhomk8t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n44tb4r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_nf78hfm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_nqh6398 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ppl3r45 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qsq36pn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vu26gaj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_expr_may_exit_qfnn2ei {
    gm_frame h;
    void * l0; /* g */
    gm_t9805qtonbsag l2; /* spec_ */
    gm_tsth9qu1dv0g3 l8; /* callee */
    uint8_t ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_7u87bne {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_93mqgd8 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_foreign_decl_rld7m78 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* params */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_frame_done_014nc4i {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_gen_entry_hj4odev {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* s */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_gen_exports_koai3gr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij l10; /* params */
    gm_trqfvmjtt4sij l11; /* args */
    gm_trqfvmjtt4sij l12; /* stores */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_gen_main_0gsgsok {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* s */
    gm_trqfvmjtt4sij l4; /* calls */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_gen_start_ip4boq0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_generate_6no5tn1 {
    gm_frame h;
    gm_tetdkgoreqmn1 ret;
};
struct gm_fr_generate_entries_br1bd7f {
    gm_frame h;
    gm_trg1s7erfs461 l5; /* <tmp> */
    gm_t4c7ipjj9s3fp l7; /* <tmp> */
    gm_t4c7ipjj9s3fp l8; /* <tmp> */
    gm_ttiitul0263ld l9; /* <tmp> */
    gm_trqfvmjtt4sij l10; /* <tmp> */
    gm_trqfvmjtt4sij l11; /* <tmp> */
    gm_trqfvmjtt4sij l12; /* <tmp> */
    gm_t4c7ipjj9s3fp l13; /* <tmp> */
    gm_t4c7ipjj9s3fp l14; /* <tmp> */
    gm_t4c7ipjj9s3fp l15; /* <tmp> */
    gm_trqfvmjtt4sij l16; /* <tmp> */
    gm_t4c7ipjj9s3fp l17; /* <tmp> */
    gm_trqfvmjtt4sij l18; /* <tmp> */
    gm_t4c7ipjj9s3fp l19; /* <tmp> */
    gm_trqfvmjtt4sij l20; /* <tmp> */
    gm_trqfvmjtt4sij l21; /* <tmp> */
    gm_trqfvmjtt4sij l22; /* <tmp> */
    gm_trqfvmjtt4sij l23; /* <tmp> */
    gm_te68172gglous l24; /* <tmp> */
    gm_trqfvmjtt4sij l25; /* <tmp> */
    gm_trqfvmjtt4sij l26; /* <tmp> */
    gm_trqfvmjtt4sij l27; /* <tmp> */
    gm_t4c7ipjj9s3fp l28; /* <tmp> */
    gm_t4c7ipjj9s3fp l29; /* <tmp> */
    gm_trqfvmjtt4sij l30; /* <tmp> */
    gm_trqfvmjtt4sij l31; /* <tmp> */
    gm_t4c7ipjj9s3fp l32; /* <tmp> */
    gm_t4c7ipjj9s3fp l33; /* <tmp> */
    gm_trqfvmjtt4sij l34; /* <tmp> */
    gm_t4c7ipjj9s3fp l35; /* <tmp> */
    gm_t4c7ipjj9s3fp l36; /* <tmp> */
    gm_trqfvmjtt4sij l37; /* <tmp> */
    gm_trqfvmjtt4sij l38; /* <tmp> */
    gm_trqfvmjtt4sij l39; /* <tmp> */
    gm_trqfvmjtt4sij l40; /* <tmp> */
    gm_trqfvmjtt4sij l41; /* <tmp> */
    gm_trqfvmjtt4sij l42; /* <tmp> */
    gm_trqfvmjtt4sij l43; /* <tmp> */
    gm_trqfvmjtt4sij l44; /* <tmp> */
    gm_t4c7ipjj9s3fp l45; /* <tmp> */
    gm_t4c7ipjj9s3fp l46; /* <tmp> */
    gm_trqfvmjtt4sij l47; /* <tmp> */
    gm_t4c7ipjj9s3fp l48; /* <tmp> */
    gm_trqfvmjtt4sij l49; /* <tmp> */
    gm_t4c7ipjj9s3fp l50; /* <tmp> */
    gm_tpdgmavtftccj l51; /* g */
    gm_trqfvmjtt4sij l64; /* head */
    gm_trqfvmjtt4sij l67; /* data */
    gm_trqfvmjtt4sij l73; /* out */
    uint64_t l74; /* data_lo */
    uint64_t l75; /* data_hi */
    gm_tk5njge13tk1d l95; /* parts */
    gm_tetdkgoreqmn1 ret;
};
struct gm_fr_generate_object_rgfvem4 {
    gm_frame h;
    gm_tetdkgoreqmn1 ret;
};
struct gm_fr_generate_tests_462i0us {
    gm_frame h;
    gm_tetdkgoreqmn1 ret;
};
struct gm_fr_get_ag7vhtq {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_get_dk5gh28 {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_get_ehtj21s {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_handler_decl_m66mgr8 {
    gm_frame h;
    void * ret;
};
struct gm_fr_handler_lit_b02boff {
    gm_frame h;
    void * ret;
};
struct gm_fr_handler_table_2kdtlit {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* names */
    gm_b16_8 ret;
};
struct gm_fr_hash_map_1c18lnh {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_1s9eugt {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_41r6dkn {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_70470jk {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_76d05hh {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_em2q005 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_fupfd5g {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_g5h3sdu {
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
struct gm_fr_hash_map_p3t5tj8 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_rs5nlfs {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_t9h7o8p {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_tl7apid {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_eo6jkc5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_never_in5ftma {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_plain_qr08i40 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_17uennd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_void_vs2outo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_line_directive_fjg94gc {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_list_0c2v515 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0vc6l5i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1t0a5et {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_938p55h {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_a5k8eig {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_av8bg03 {
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
struct gm_fr_list_hme58st {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_k5efes8 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_lj5vg1g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rc6kc0u {
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
struct gm_fr_mark_owner_uhhhf9f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_may_unwind_irl2ktm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_mirror_text_cr5muc9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_needs_wrapper_ktlbb7i {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_new_namer_5q88o9g {
    gm_frame h;
    gm_ttiitul0263ld ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_owned_d5psnvc {
    gm_frame h;
    gm_t4c7ipjj9s3fp l1; /* by_file */
    gm_trqfvmjtt4sij l2; /* paths */
    gm_trqfvmjtt4sij l3; /* texts */
    gm_trqfvmjtt4sij l12; /* out */
    gm_b16_8 ret;
};
struct gm_fr_owner_of_cirm596 {
    gm_frame h;
    gm_tgqgdb97ka5kt ret;
};
struct gm_fr_param_cty_e8tvhrm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pop_168lbb8 {
    gm_frame h;
    gm_t736djc66po0l ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_queue_cd51r8c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_queue_spec_khngsmv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_spec_bitccmf {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_spec_depth_em7r6vt {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_spec_handler_table_8h552cp {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* names */
    gm_b16_8 ret;
};
struct gm_fr_spec_key_eljlge7 {
    gm_frame h;
    gm_tf9f5m02f56gm ret;
};
struct gm_fr_spec_slot_jild38d {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_src_loc_lrhtj7i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_src_name_0r539f2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_static_bytes_u0uvco3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_takes_fn_n239ee5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
struct gm_fr_view_defs_u461okc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_with_helpers_edhoqtc {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* out */
    gm_b16_8 ret;
};
struct gm_fr_within_sys_2lc2ugq {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_absolute_6pqhsaj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_name_g5lrlt3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_adt_mpqv8qe(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_typedefs_4nfn4l8(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_c_abi_type_nu2c2tm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_callee_name_4k9m9s6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_clause_may_exit_7lc6flj(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_clause_name_uk8dps6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2);
__attribute__((aligned(4))) uint8_t gm_f_code_has_arak3mp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_content_name_83mn5uf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_ctypes_j6au0st(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_decl_name_fode8m9(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_decl_table_l7u1k0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_desc_0cag2kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_descriptors_fnnm3h1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_drain_leoibma(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint32_t gm_f_effect_id_5588nqa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_08mdrht(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_13d6rhn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2ntv1ru(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_360e8h0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_465e48m(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7hq6cu2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_al38tsc(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_asvj6q6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e8istsh(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ednoq9d(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ekr3omu(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gm6v230(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_h06rie2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jagk2mc(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jjmeosp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kge4ibg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mhomk8t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n44tb4r(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nf78hfm(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nqh6398(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void);
__attribute__((aligned(4))) uint8_t gm_f_expr_may_exit_qfnn2ei(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9805qtonbsag a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_msip1bg(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_csk9ups(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_efqiqfc(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_hb6lt4h(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_93mqgd8(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_key_b6f5f24(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_name_6n34573(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_foreign_decl_rld7m78(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_2g08abe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3g143bv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5029eea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6g8g0f7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6gtuelb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6vickkk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_7fi363q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
__attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8v2pi8t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_99gc605(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_avhd7c9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_b6qg4rq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_be3fee1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bi036vc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dcns5hm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_de12mga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
__attribute__((aligned(4))) void gm_f_format_dgfvefu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eagqd9c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eg7p593(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_en16lfl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eq87866(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fpa95lg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_k64sjdb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kj13ck1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_l05am3t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_lrgko01(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_o1q76rb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_or32na5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t3l2mea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_uaq30dv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v6efj7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vj1olp7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vmlgaaq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vp0clr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_frame_done_014nc4i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_gdb_script_section_320gurp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_gen_consts_6ibghrc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_gen_entry_hj4odev(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_gen_exports_koai3gr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_gen_fn_2fqvb90(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2);
__attribute__((aligned(4))) void gm_f_gen_main_0gsgsok(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_gen_start_ip4boq0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_gen_trap_hook_lu55viv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_generate_6no5tn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) void gm_f_generate_entries_br1bd7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, const void * a3);
inline __attribute__((aligned(4))) void gm_f_generate_object_rgfvem4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_generate_tests_462i0us(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_get_9k2rdij(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ag7vhtq(void * a0, gm_tcc1rq4n1avb2 a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_dk5gh28(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ehtj21s(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_gnmase9(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_m1knqqk(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pftnkei(void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_guard_o7k9a0e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_handler_decl_m66mgr8(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_handler_lit_b02boff(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_handler_name_6km8n3f(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_handler_table_2kdtlit(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint64_t gm_f_hash64_apkb7bo(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_1c18lnh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_1s9eugt(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_41r6dkn(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_70470jk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_76d05hh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_em2q005(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_fupfd5g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_g5h3sdu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_lljdp3h(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_rs5nlfs(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_t9h7o8p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_insert_v6fq3iv(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_is_ctl_oclllo3(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_qr08i40(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_line_directive_fjg94gc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0vc6l5i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1t0a5et(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_938p55h(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_a5k8eig(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_av8bg03(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_hme58st(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_k5efes8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_lj5vg1g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rc6kc0u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_mark_owner_uhhhf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tgqgdb97ka5kt a1);
__attribute__((aligned(4))) uint8_t gm_f_may_unwind_irl2ktm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_member_names_17el54k(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_mirror_lcjegsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mirror_text_cr5muc9(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_needs_wrapper_ktlbb7i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_new_helpers_4q783cn(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_new_namer_5q88o9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_owned_d5psnvc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tgqgdb97ka5kt gm_f_owner_of_cirm596(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t736djc66po0l gm_f_pop_168lbb8(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_97b8r3k(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_cn89s92(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_jttnn67(void * a0, gm_tf9f5m02f56gm a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_lm9fgpm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_sgikgd8(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_074448v(void * a0, gm_b16_8 a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3g1tdpr(void * a0, gm_tcc1rq4n1avb2 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_bii31he(void * a0, gm_tl874t2atnlh3 a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_cmvik6u(void * a0, gm_tcc1rq4n1avb2 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_dm7u1sg(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_po09vud(void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qoojoqv(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_v6g5pup(void * a0, uint32_t a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_queue_cd51r8c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_queue_spec_khngsmv(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint32_t gm_f_spec_bitccmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tf9f5m02f56gm a1);
inline __attribute__((aligned(4))) uint32_t gm_f_spec_depth_em7r6vt(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_handler_table_8h552cp(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_tf9f5m02f56gm gm_f_spec_key_eljlge7(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_name_jptsbbf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint32_t gm_f_spec_slot_jild38d(gm_t9805qtonbsag a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_text_iq7r8vn(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_src_name_0r539f2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_static_bytes_u0uvco3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_takes_fn_n239ee5(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_view_defs_u461okc(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_with_helpers_edhoqtc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_within_sys_2lc2ugq(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_08b77ko(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2uh64vr(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_4uefpre(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6rjcu4v(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8o29aab(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_9falfvm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_b3knhd6(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_c5hlb1u(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_d85j2rp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gi12t4o(void * a0, gm_t19enj90vg8bd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gk1hd5k(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hclpi1b(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_j5p5p7n(void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jqf697l(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kdsk4q9(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lu60214(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_okup0j2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_pfrpr5k(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qph5tm9(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qqu2rl9(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_sf9mjak(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ticpdcl(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u3br5ci(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u8u1amh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ug67q96(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_usci3o4(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uuia5cm(void * a0, const void * a1);
extern uint8_t gm_c9e8ns5bad4[16] __attribute__((aligned(8)));
extern uint8_t gm_ccmgjp6th6r[16] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cgsmfcq937e_2[16] __attribute__((aligned(8)));
extern uint8_t gm_cto913mafo2[8] __attribute__((aligned(8)));
extern uint8_t gm_ctvsbqjhdl3[12] __attribute__((aligned(4)));
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s10iv34fu7qe7[3];
extern const uint8_t gm_s205esok2u8pk[4];
extern const uint8_t gm_s2gnpj7k4l22n[2];
extern const uint8_t gm_s2kvk8bnjk52t[12];
extern const uint8_t gm_s2nn3moc8tfri[17];
extern const uint8_t gm_s34o1ojfv2p3d[17];
extern const uint8_t gm_s3nnnhoq632g4[14];
extern const uint8_t gm_s4ql65vkv7h9p[13];
extern const uint8_t gm_s514u4evks75f[21];
extern const uint8_t gm_s5vn66tnlipdf[19];
extern const uint8_t gm_s698sbcv3nmjk[69];
extern const uint8_t gm_s6aq8jknpl0n5[27];
extern const uint8_t gm_s8rsp9val5ful[5];
extern const uint8_t gm_sar77rocuptb5[31];
extern const uint8_t gm_sasi3vvkcdvo0[40];
extern const uint8_t gm_scj0sm2kpivk4[9];
extern const uint8_t gm_scj1kuqp0242l[6];
extern const uint8_t gm_sck2rq9li012q[23];
extern const uint8_t gm_scrjehcehrfpv[15];
extern const uint8_t gm_sdbocdqbukr6k[77];
extern const uint8_t gm_sdi44r3kug6n6[14];
extern const uint8_t gm_sdnjmvrkc1r7r[12];
extern const uint8_t gm_sekmbu83qkg9c[44];
extern const uint8_t gm_seosa3iobfc09[35];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_shlk3g0tk7m4f[31];
extern const uint8_t gm_siji3oo7ud53t[10];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sje4rbc88csdu[7];
extern const uint8_t gm_skdpq4t6o94o2[25];
extern const uint8_t gm_slbs27elv52b5[30];
extern const uint8_t gm_slku641rnhls8[7];
extern const uint8_t gm_smbbaf3n1gml2[8];
extern const uint8_t gm_smdv045psk1u8[25];
extern const uint8_t gm_snokcvhtidt97[6];
extern const uint8_t gm_sobumja6poqii[13];
extern const uint8_t gm_soo4rtckloi4d[13];
extern const uint8_t gm_soqt3jnmovq5l[4];
extern const uint8_t gm_sp5lvq92b4in0[11];
extern const uint8_t gm_spervk999ikim[8];
extern const uint8_t gm_sq77ose3aa457[24];
extern const uint8_t gm_sq9pusqm0kq2f[7];
extern const uint8_t gm_sqg9do4ma3giu[6];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sr3lr7c9psgf2[12];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srckfibkpt681aa[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuj9anpi7p1mq[];
extern const char gm_srcuokv9ad9oqqb[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_sreie3avjjb9l[64];
extern const uint8_t gm_srjb0lv60gms5[62];
extern const uint8_t gm_strca8t49bpqu[2];
extern const uint8_t gm_sts5j6f5pkov0[5];
extern const uint8_t gm_sucsarpa3psmp[55];
extern const uint8_t gm_suqchf5vh2hqp[5];
extern const uint8_t gm_sv7tbc2gvmpa1[100];
extern const uint8_t gm_svgsom6hbqtcn[40];
extern const uint8_t gm_svl13be5ca8rt[5];
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq467d5cm1d5(a, b);
}
inline __attribute__((aligned(4))) void * gm_f_adt_mpqv8qe(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_mpqv8qe F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1088) + 0) + 8);
    if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcuj9anpi7p1mq, 135, 63);
    void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t3 * 184);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_child_ffc8h1k F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        void * t2 = gm_l0;
        gm_closure t3 = ((gm_closure){(void *)gm_f_fn_7u87bne, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t4 = F.l2;
        uint8_t t5 = 0;
        F.ret = t4;
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
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_by_ref_fi0o860 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t5v8n6qrn4rh4 t4;
    gm_f_repr_53quqki((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)6ULL)));
    if (t5) {
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = gm_l1;
        uint64_t t8 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t9 = (t8 > ((uint64_t)16ULL));
        t1 = t9;
    } else t1 = 0;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_callee_name_4k9m9s6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_callee_name_4k9m9s6 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        gm_b16_8 t4 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t5 = 0;
        F.ret = t4;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_content_name_83mn5uf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_content_name_83mn5uf F;
    gm_b16_8 gm_l0; /* prefix */
    gm_b16_8 gm_l1; /* content */
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t6j6smj7qoctu t1;
    gm_b16_8 t2 = gm_l0;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t3, ((uint64_t)12ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 16, t4);
    gm_tabb710d9mq2e t5;
    gm_f_format_8n626pi((void *)&t5, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcht195tfrfuqo, 84, 82);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_conv_attr_m0b89pc F;
    uint8_t gm_l0; /* win64 */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t3 = gm_l0;
    if (!t3) goto ie2;
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_skdpq4t6o94o2));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(24));
    gm_b16_8 t6 = t5;
    t1 = t6;
    goto ix2;
ie2:;
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(0));
    gm_b16_8 t9 = t8;
    t1 = t9;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_descriptors_fnnm3h1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_descriptors_fnnm3h1 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tfeab279c16rr t1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 288) + 0));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t3);
    void * t4 = gm_l0;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 328) + 0));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 16, t5);
    void * t6 = gm_l0;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)t6) + 368) + 0));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 32, t7);
    gm_tabb710d9mq2e t8;
    gm_f_format_o1q76rb((void *)&t8, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_srckfibkpt681aa, 627, 63);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_08mdrht(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_08mdrht F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_13d6rhn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_13d6rhn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2ntv1ru(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2ntv1ru F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_360e8h0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_360e8h0 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_al38tsc(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_al38tsc F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_asvj6q6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_asvj6q6 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 104)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e8istsh(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_e8istsh F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 2)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ednoq9d(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ednoq9d F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ekr3omu(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ekr3omu F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_h06rie2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_h06rie2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jagk2mc(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jagk2mc F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jjmeosp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jjmeosp F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kge4ibg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kge4ibg F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mhomk8t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_mhomk8t F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n44tb4r(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n44tb4r F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nf78hfm(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_nf78hfm F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nqh6398(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_nqh6398 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ppl3r45 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qsq36pn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vu26gaj F;
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
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_o15fqku F;
    void * gm_l0; /* sm */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcv1nri2247d2r, 78, 75);
    void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 80);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_7u87bne F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            gm_closure t6 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l1));
            void * t7 = gm_l0;
            gm_closure t8 = t6;
            gm_node *ccx9 = t8.cx;
            void * t10 = t8.code;
            uint8_t t11;
            if (GM_PTR_TAGGED(t10)) {
                t11 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t10))(t7);
            } else {
                t11 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t10)(t8.env, ccx9, NULL, t7);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2), t11);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_93mqgd8(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_93mqgd8 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_expr_may_exit_qfnn2ei *)((&F.h)->up))->l0));
    void * t2 = gm_l0;
    gm_t9805qtonbsag t3 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&((struct gm_fr_expr_may_exit_qfnn2ei *)((&F.h)->up))->l2));
    uint8_t t4 = gm_f_expr_may_exit_qfnn2ei((void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_gen_start_ip4boq0(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_gen_start_ip4boq0 F;
    void * gm_l0; /* g */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        int32_t t2;
        void * t4 = gm_l0;
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 1136) + 1));
        if (!t5) goto ie3;
        int32_t t7 = ((int32_t)1LL);
        t2 = t7;
        goto ix3;
ie3:;
        int32_t t9 = ((int32_t)0LL);
        t2 = t9;
ix3:;
        gm_t19enj90vg8bd t10;
        GM_ST(int32_t, ((uint8_t *)&t10 + 0), t2);
        gm_tabb710d9mq2e t11;
        gm_f_format_de12mga((void *)&t11, (void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_srcuokv9ad9oqqb, 9, 12);
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
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
inline __attribute__((aligned(4))) void gm_f_generate_object_rgfvem4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_generate_object_rgfvem4 F;
    void * gm_l0; /* p */
    gm_tftmsj5vtqvej gm_l1; /* opts */
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_f_empty_slice_d914i5u();
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
        gm_f_generate_entries_br1bd7f((void *)&(*(gm_tetdkgoreqmn1 *)gm_out), (void *)NULL, cx, NULL, t2, t4, ((uint8_t)0), &(*(gm_tftmsj5vtqvej *)((uint8_t *)&gm_l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void gm_f_generate_tests_462i0us(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_generate_tests_462i0us F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* entries */
    gm_tftmsj5vtqvej gm_l2; /* opts */
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_f_generate_entries_br1bd7f((void *)&(*(gm_tetdkgoreqmn1 *)gm_out), (void *)NULL, cx, NULL, t2, t3, ((uint8_t)1), &(*(gm_tftmsj5vtqvej *)((uint8_t *)&gm_l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ag7vhtq(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ag7vhtq F;
    void * gm_l0; /* m */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 115u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_efqiqfc(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tqs5p4n2tq19v t7;
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
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 16) + 12));
        gm_tqs5p4n2tq19v t14;
        GM_ST(uint8_t, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[1] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_dk5gh28(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_dk5gh28 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 111u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_hb6lt4h(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tqs5p4n2tq19v t7;
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
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 12) + 8));
        gm_tqs5p4n2tq19v t14;
        GM_ST(uint8_t, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[1] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ehtj21s(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ehtj21s F;
    void * gm_l0; /* m */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 115u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_csk9ups(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tqs5p4n2tq19v t7;
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
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 16) + 12));
        gm_tqs5p4n2tq19v t14;
        GM_ST(uint8_t, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[1] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_handler_decl_m66mgr8(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_handler_decl_m66mgr8 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 544) + 0) + 8);
    if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcuj9anpi7p1mq, 132, 83);
    void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 544) + 0)) + t3 * 64);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_handler_lit_b02boff(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_handler_lit_b02boff F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* h */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 504) + 0) + 8);
    if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcuj9anpi7p1mq, 129, 76);
    void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 504) + 0)) + t3 * 40);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_1c18lnh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_1c18lnh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_13d6rhn();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_1s9eugt(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_1s9eugt F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_08mdrht();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_41r6dkn(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_41r6dkn F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_mhomk8t();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_70470jk(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_70470jk F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_n44tb4r();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_76d05hh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_76d05hh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_ekr3omu();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_em2q005(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_em2q005 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_nf78hfm();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_fupfd5g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_fupfd5g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_360e8h0();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_g5h3sdu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_g5h3sdu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_h06rie2();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_p3t5tj8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_vu26gaj();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_rs5nlfs(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_rs5nlfs F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_jagk2mc();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_tl7apid F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_qsq36pn();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_eo6jkc5 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_is_integer_8d4f9ek(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_never_in5ftma F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_t53at80pfdus3 t5;
    gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
    uint8_t t6 = ((GM_LD(uint8_t, ((uint8_t *)&t5) + 0) == ((uint8_t)5ULL)));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_17uennd F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_is_unit_n83ttnb(t3, GM_CONV(gm_csguankko4lmhd, t4));
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
    GM_STACK_CHECK_SHALLOW(&F, 126u);
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
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t7;
        {
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t13 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_void_vs2outo F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint8_t t4 = gm_f_is_unit_17uennd(t2, GM_CONV(gm_csguankko4lmhd, t3));
    if (!t4) {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint8_t t7 = gm_f_is_never_in5ftma(t5, GM_CONV(gm_csguankko4lmhd, t6));
        t1 = t7;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
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
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0c2v515 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ppl3r45();
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
inline __attribute__((aligned(4))) void gm_f_list_0vc6l5i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0vc6l5i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_e8istsh();
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
inline __attribute__((aligned(4))) void gm_f_list_1t0a5et(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1t0a5et F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_al38tsc();
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
inline __attribute__((aligned(4))) void gm_f_list_938p55h(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_938p55h F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_jjmeosp();
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
inline __attribute__((aligned(4))) void gm_f_list_a5k8eig(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_a5k8eig F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_2ntv1ru();
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
inline __attribute__((aligned(4))) void gm_f_list_av8bg03(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_av8bg03 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_nqh6398();
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
inline __attribute__((aligned(4))) void gm_f_list_hme58st(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_hme58st F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ednoq9d();
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
inline __attribute__((aligned(4))) void gm_f_list_k5efes8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_k5efes8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_kge4ibg();
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
inline __attribute__((aligned(4))) void gm_f_list_rc6kc0u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rc6kc0u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_asvj6q6();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mirror_text_cr5muc9(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mirror_text_cr5muc9 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)t1) + 712) + 0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_new_namer_5q88o9g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_namer_5q88o9g F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t4c7ipjj9s3fp t1;
    gm_f_hash_map_m04clhi((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_ttiitul0263ld *)gm_out) + 0, t1);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_of_e6hfj1r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 16));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_param_cty_e8tvhrm F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 149u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (!t5) goto ie2;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sqtc9fffut4s2));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(12));
    gm_b16_8 t8 = t7;
    t1 = t8;
    goto ix2;
ie2:;
    void * t10 = gm_l0;
    gm_t88rcucg1b7uk t11 = gm_l1;
    gm_b16_8 t12 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t13 = t12;
    t1 = t13;
ix2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t736djc66po0l gm_f_pop_168lbb8(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_168lbb8 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_tcc1rq4n1avb2 gm_l2; /* x */
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
            gm_t736djc66po0l t8;
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
        gm_tcc1rq4n1avb2 t17 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 8));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_tcc1rq4n1avb2 t29 = gm_l2;
        gm_t736djc66po0l t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 10, 0);
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[8] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_570g79n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint32_t gm_l2; /* x */
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
            gm_t9805qtonbsag t8;
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
        uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 4));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        uint32_t t29 = gm_l2;
        gm_t9805qtonbsag t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[4] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 129u);
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
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        uint8_t t9 = 0;
        F.ret = t8;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_ref_aqt1t7c F;
    void * gm_l0; /* t */
    void * gm_l1; /* e */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 137u);
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
        void * t5 = gm_l1;
        gm_b0_1 t6 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        uint8_t t9 = 0;
        F.ret = t8;
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_a5ovnrm((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
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
inline __attribute__((aligned(4))) uint32_t gm_f_spec_depth_em7r6vt(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_spec_depth_em7r6vt F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* s */
    gm_tf9f5m02f56gm gm_l2; /* key */
    uint32_t gm_l3; /* o */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        gm_tf9f5m02f56gm t4 = gm_f_spec_key_eljlge7(t2, t3);
        gm_l2 = t4;
    }
    {
        if (!(((((uint8_t *)&gm_l2) + 8))[4] != 0)) goto ie5;
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
        gm_l3 = t6;
        {
            void * t8 = gm_l0;
            uint32_t t9 = gm_l3;
            uint32_t t10 = gm_f_spec_depth_em7r6vt(t8, t9);
            uint32_t r11; bool o12 = __builtin_add_overflow(((uint32_t)1ULL), t10, &r11);
            if (o12) gm_trap("integer overflow in `+`", gm_srcd5arorrf8jkg, 353, 32);
            uint32_t t13 = r11;
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        uint8_t t15 = 0;
        F.ret = ((uint32_t)1ULL);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf9f5m02f56gm gm_f_spec_key_eljlge7(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_spec_key_eljlge7 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* s */
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
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 2592) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcd5arorrf8jkg, 348, 53);
    gm_tf9f5m02f56gm t6 = GM_LD(gm_tf9f5m02f56gm, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 2592) + 0)) + t3 * 16));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_spec_slot_jild38d(gm_t9805qtonbsag a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_spec_slot_jild38d F;
    gm_t9805qtonbsag gm_l0; /* outer */
    uint32_t gm_l1; /* o */
    GM_STACK_CHECK_SHALLOW(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        if (!((((uint8_t *)&gm_l0))[4] != 0)) goto ie2;
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&gm_l0));
        gm_l1 = t3;
        {
            uint32_t t5 = gm_l1;
            uint32_t r6; bool o7 = __builtin_add_overflow(t5, ((uint32_t)1ULL), &r6);
            if (o7) gm_trap("integer overflow in `+`", gm_srcd5arorrf8jkg, 359, 28);
            uint32_t t8 = r6;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t10 = 0;
        F.ret = ((uint32_t)0ULL);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_src_name_0r539f2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_src_name_0r539f2 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* file */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1;
    GM_ST(void *, (uint8_t *)&t1, (void *)(gm_slku641rnhls8));
    GM_ST(uintptr_t, (uint8_t *)&t1 + 8, (uintptr_t)(6));
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
    uint32_t t7 = gm_l1;
    void * t8 = gm_f_file_o15fqku(t6, t7);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 0));
    gm_b16_8 t10 = gm_f_content_name_83mn5uf((void *)NULL, cx, NULL, t1, t9);
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
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_unsupported_531q51r F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        memset((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0, 4);
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_tabb710d9mq2e t4;
        gm_f_format_fsfutor((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcd5arorrf8jkg, 35, 27);
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_tl874t2atnlh3 t7 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0, t6);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16, t7);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_view_defs_u461okc(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_defs_u461okc F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((((uint8_t *)t1) + 872) + 104) + 0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_addr_name_g5lrlt3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_addr_name_g5lrlt3 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 187u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint32_t t4 = gm_l1;
        uint8_t t5 = gm_f_needs_wrapper_ktlbb7i((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            uint32_t t8 = gm_l1;
            gm_b16_8 t9 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tabb710d9mq2e t11;
            gm_f_format_bi036vc((void *)&t11, (void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_srcd5arorrf8jkg, 889, 36);
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t15 = gm_l0;
        uint32_t t16 = gm_l1;
        gm_b16_8 t17 = gm_f_callee_name_4k9m9s6((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t18 = 0;
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
inline __attribute__((aligned(4))) void gm_f_c_abi_type_nu2c2tm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_c_abi_type_nu2c2tm F;
    void * gm_l0; /* g */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* m */
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t5v8n6qrn4rh4 t5;
        gm_f_repr_53quqki((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; default: goto ma9; }
ma7:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t13 != ((uint8_t)2ULL)) goto mn12;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_snokcvhtidt97));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(5));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t14);
        goto gm_fdone;
        goto me6;
mn12:;
ma8:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t18 != ((uint8_t)0ULL)) goto pa17;
        goto po16;
pa17:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t20 != ((uint8_t)1ULL)) goto pa19;
        goto po16;
pa19:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t22 != ((uint8_t)3ULL)) goto pa21;
        goto po16;
pa21:;
        goto mn15;
po16:;
        void * t23 = gm_l0;
        void * t24 = (void *)(((uint8_t *)t23) + 8);
        gm_t88rcucg1b7uk t25 = gm_l1;
        gm_b16_8 t26 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t26);
        goto gm_fdone;
        goto me6;
mn15:;
ma9:;
        {
            void * t30 = gm_l0;
            void * t31 = (void *)(((uint8_t *)t30) + 8);
            gm_t88rcucg1b7uk t32 = gm_l1;
            gm_th2lqbo77pvsc t33;
            gm_f_mirror_lcjegsf((void *)&t33, (void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t33))[16] != 0)) goto ie29;
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
            gm_l3 = t34;
            {
                gm_b16_8 t36 = gm_l3;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t36);
                goto gm_fdone;
            }
            goto ix29;
ie29:;
ix29:;
        }
        {
            gm_tl874t2atnlh3 t37 = gm_l2;
            void * t38 = gm_l0;
            void * t39 = (void *)(((uint8_t *)t38) + 8);
            gm_t88rcucg1b7uk t40 = gm_l1;
            gm_b16_8 t41 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t42;
            GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
            gm_tabb710d9mq2e t43;
            gm_f_format_b6qg4rq((void *)&t43, (void *)NULL, cx, NULL, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcd5arorrf8jkg, 920, 39);
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
            gm_t08olq97rscn3 t46;
            gm_f_unsupported_531q51r((void *)&t46, (void *)NULL, cx, NULL, t37, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t46);
            goto gm_fdone;
        }
        goto me6;
mn27:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 915, 5);
me6:;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_callee_name_4k9m9s6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_clause_may_exit_7lc6flj(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2) {
    struct gm_fr_clause_may_exit_7lc6flj F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* c */
    gm_t9805qtonbsag gm_l2; /* outer */
    gm_tcc1rq4n1avb2 gm_l3; /* key */
    uint8_t gm_l4; /* m */
    uint8_t gm_l5; /* r */
    void * gm_l6; /* b */
    GM_STACK_CHECK(&F, 221u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l1;
        gm_t9805qtonbsag t3 = gm_l2;
        uint32_t t4 = gm_f_spec_slot_jild38d(t3);
        gm_tcc1rq4n1avb2 t5;
        GM_ST(uint32_t, ((uint8_t *)&t5 + 0), t2);
        GM_ST(uint32_t, ((uint8_t *)&t5 + 4), t4);
        gm_l3 = t5;
    }
    {
        void * t7 = gm_l0;
        void * t8 = (void *)(((uint8_t *)t7) + 2840);
        gm_tcc1rq4n1avb2 t9 = gm_l3;
        gm_tqs5p4n2tq19v t10 = gm_f_get_ehtj21s(t8, t9);
        if (!((((uint8_t *)&t10))[1] != 0)) goto ie6;
        uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)&t10));
        gm_l4 = t11;
        {
            uint8_t t13 = gm_l4;
            uint8_t t14 = (!(t13 == ((uint8_t)2ULL)));
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t16 = gm_l0;
        void * t17 = (void *)(((uint8_t *)t16) + 2840);
        gm_tcc1rq4n1avb2 t18 = gm_l3;
        gm_tftb1odc739t5 t19 = gm_f_put_cmvik6u(t17, t18, ((uint8_t)1ULL));
        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 1, gm_srcd5arorrf8jkg, 782, 5);
        gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 1));
    }
    {
        gm_l5 = ((uint8_t)1);
    }
    {
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
        uint32_t t24 = gm_l1;
        void * t25 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 152));
        if (!(GM_LD(void *, ((uint8_t *)&t26)) != 0)) goto ie21;
        void * t27 = GM_LD(void *, ((uint8_t *)&t26));
        gm_l6 = t27;
        {
            void * t29 = gm_l0;
            void * t30 = gm_l6;
            gm_t9805qtonbsag t31 = gm_l2;
            uint8_t t32 = gm_f_expr_may_exit_qfnn2ei((void *)NULL, cx, NULL, t29, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t32;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t33 = gm_l0;
        void * t34 = (void *)(((uint8_t *)t33) + 2840);
        gm_tcc1rq4n1avb2 t35 = gm_l3;
        uint8_t t36;
        uint8_t t38 = gm_l5;
        if (!t38) goto ie37;
        uint8_t t40 = ((uint8_t)3ULL);
        t36 = t40;
        goto ix37;
ie37:;
        uint8_t t42 = ((uint8_t)2ULL);
        t36 = t42;
ix37:;
        gm_tftb1odc739t5 t43 = gm_f_put_cmvik6u(t34, t35, t36);
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcd5arorrf8jkg, 785, 5);
        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
    }
    {
        uint8_t t45 = gm_l5;
        uint8_t t46 = 0;
        F.ret = t45;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_clause_name_uk8dps6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2) {
    struct gm_fr_clause_name_uk8dps6 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* c */
    gm_t9805qtonbsag gm_l2; /* outer */
    uint32_t gm_l3; /* o */
    GM_STACK_CHECK(&F, 134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        if (!((((uint8_t *)&gm_l2))[4] != 0)) goto ie2;
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
        gm_l3 = t3;
        {
            void * t5 = gm_l0;
            uint32_t t6 = gm_l1;
            uint32_t t7 = gm_l3;
            gm_b16_8 t8 = gm_f_queue_spec_khngsmv((void *)NULL, cx, NULL, t5, t6, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        uint32_t t11 = gm_l1;
        gm_b0_1 t12 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = gm_l0;
        uint32_t t14 = gm_l1;
        gm_b16_8 t15 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t16 = 0;
        F.ret = t15;
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
inline __attribute__((aligned(4))) uint8_t gm_f_code_has_arak3mp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_code_has_arak3mp F;
    gm_b16_8 gm_l0; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint64_t t2 = GM_LD(uintptr_t, (((uint8_t *)&((struct gm_fr_generate_entries_br1bd7f *)((&F.h)->up))->l73) + 0) + 8);
    void * t3 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&((struct gm_fr_generate_entries_br1bd7f *)((&F.h)->up))->l73) + 0)));
    uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_generate_entries_br1bd7f *)((&F.h)->up))->l74));
    uint64_t t6 = (uintptr_t)t5;
    if (t4 > t6 || t6 > (uintptr_t)t2) gm_trap_slice(t4, t6, t2, gm_srcd5arorrf8jkg, 274, 60);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t3 + t4 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6 - t4));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_b16_8 t9 = gm_l0;
    uint8_t t10 = gm_f_contains_gkki4kq(t8, t9);
    if (!t10) {
        uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)&((struct gm_fr_generate_entries_br1bd7f *)((&F.h)->up))->l73) + 0) + 8);
        void * t12 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&((struct gm_fr_generate_entries_br1bd7f *)((&F.h)->up))->l73) + 0)));
        uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_generate_entries_br1bd7f *)((&F.h)->up))->l75));
        uint64_t t14 = (uintptr_t)t13;
        if (t14 > (uintptr_t)t11 || (uintptr_t)t11 > (uintptr_t)t11) gm_trap_slice(t14, (uintptr_t)t11, t11, gm_srcd5arorrf8jkg, 274, 107);
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t12 + t14 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)((uintptr_t)t11 - t14));
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
        gm_b16_8 t17 = gm_l0;
        uint8_t t18 = gm_f_contains_gkki4kq(t16, t17);
        t1 = t18;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0);
__attribute__((aligned(4))) void gm_f_decl_table_l7u1k0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_decl_table_l7u1k0g F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* d */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* k */
    uint32_t gm_l6; /* c */
    void * gm_l7; /* def */
    gm_b16_8 gm_l8; /* ret */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint32_t gm_l12; /* p */
    gm_b16_8 gm_l13; /* self_ty */
    gm_b16_8 gm_l16; /* self_arg */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    gm_b16_8 gm_l22; /* name */
    gm_b16_8 gm_l23; /* ret_kw */
    GM_STACK_CHECK(&F, 2500u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 3336);
        uint32_t t5 = gm_l1;
        gm_tqs5p4n2tq19v t6 = gm_f_get_dk5gh28(t4, t5);
        uint8_t t7;
        if (((((uint8_t *)&t6))[1] != 0)) goto mn9;
        t7 = ((uint8_t)1);
        goto me8;
mn9:;
        t7 = ((uint8_t)0);
        goto me8;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 567, 8);
me8:;
        if (!t7) goto ie2;
        {
            void * t12 = gm_l0;
            void * t13 = (void *)(((uint8_t *)t12) + 3336);
            uint32_t t14 = gm_l1;
            gm_tftb1odc739t5 t15 = gm_f_put_v6g5pup(t13, t14, ((uint8_t)1));
            if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srcd5arorrf8jkg, 568, 9);
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        {
            gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t18 = gm_l0;
                void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
                uint32_t t20 = gm_l1;
                void * t21 = gm_f_handler_decl_m66mgr8(t19, t20);
                gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 48));
                gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
                gm_l3 = t23;
            }
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t25 = gm_l4;
                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t27 = (t25 < t26);
                    if (!t27) goto lb24;
                    {
                        uint64_t t30 = gm_l4;
                        uint64_t t31 = gm_l4;
                        uint64_t t32 = (uintptr_t)t31;
                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        uint32_t t34 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t32 * 4));
                        gm_tn68el8d14vku t35;
                        GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t35 + 0), t30);
                        GM_ST(uint32_t, ((uint8_t *)&t35 + 8), t34);
                        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&t35) + 0));
                        gm_l5 = t38;
                        uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&t35) + 8));
                        gm_l6 = t39;
                        {
                            void * t41 = gm_l0;
                            uint32_t t42 = gm_l6;
                            gm_b0_1 t43 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t41, t42);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                        }
                        {
                            void * t44 = gm_l0;
                            void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
                            uint32_t t46 = gm_l6;
                            void * t47 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t45, t46);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            gm_l7 = t47;
                        }
                        {
                            void * t48 = gm_l0;
                            void * t49 = (void *)(((uint8_t *)t48) + 8);
                            void * t50 = gm_l7;
                            gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t50) + 64));
                            gm_b16_8 t52 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t49, t51);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            gm_l8 = t52;
                        }
                        {
                            gm_f_list_nof7vfj((void *)&F.l9, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                        }
                        {
                            {
                                void * t54 = gm_l7;
                                gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)t54) + 32));
                                gm_l10 = t55;
                            }
                            {
                                gm_l11 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t57 = gm_l11;
                                    uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                    uint8_t t59 = (t57 < t58);
                                    if (!t59) goto lb56;
                                    {
                                        uint64_t t62 = gm_l11;
                                        uint64_t t63 = (uintptr_t)t62;
                                        uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                        uint32_t t65 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t63 * 4));
                                        uint32_t t68 = GM_LD(uint32_t, ((uint8_t *)&t65));
                                        gm_l12 = t68;
                                        {
                                            void * t70 = (void *)((uint8_t *)&F.l9);
                                            uint32_t t71 = gm_l12;
                                            uint64_t t72 = (uint64_t)t71;
                                            uint64_t t73 = (uintptr_t)t72;
                                            void * t74 = gm_l7;
                                            uint64_t t75 = GM_LD(uintptr_t, (((uint8_t *)t74) + 48) + 8);
                                            if (t73 >= (uintptr_t)(t75)) gm_trap_index(t73, t75, gm_srcd5arorrf8jkg, 575, 45);
                                            gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t74) + 48)) + t73 * 40) + 16));
                                            gm_tftb1odc739t5 t77 = gm_f_push_k3l9nic(t70, GM_CONV(gm_csguankko4lmhd, t76));
                                            if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_srcd5arorrf8jkg, 575, 35);
                                            gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
                                        }
                                        goto me66;
mn67:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 575, 13);
me66:;
                                    }
lc56:;
                                    {
                                        uint64_t t80 = gm_l11;
                                        uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                                        uint64_t t83 = r81;
                                        gm_l11 = t83;
                                    }
                                }
lb56:;
                            }
                        }
                        {
                            void * t84 = gm_l0;
                            void * t85 = (void *)(((uint8_t *)t84) + 8);
                            uint64_t t86 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t87 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                            if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcd5arorrf8jkg, 576, 33);
                            gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t86 * 4));
                            gm_b16_8 t89 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t85, t88);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            gm_l13 = t89;
                        }
                        {
                            gm_f_list_epvducg((void *)&F.l14, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                        }
                        {
                        }
                        {
                            void * t90 = (void *)((uint8_t *)&F.l14);
                            {
                                gm_b16_8 t92;
                                GM_ST(void *, (uint8_t *)&t92, (void *)(gm_siji3oo7ud53t));
                                GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(9));
                                gm_b16_8 t93;
                                GM_ST(void *, (uint8_t *)&t93, (void *)(gm_s2kvk8bnjk52t));
                                GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(11));
                                gm_b16_8 t94;
                                GM_ST(void *, (uint8_t *)&t94, (void *)(gm_sdi44r3kug6n6));
                                GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(13));
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t9bmh80edou9c *)((uint8_t *)&F.l15)) + 0, t92);
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t9bmh80edou9c *)((uint8_t *)&F.l15)) + 16, t93);
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t9bmh80edou9c *)((uint8_t *)&F.l15)) + 32, t94);
                            }
                            void * t95 = ((uint8_t *)&F.l15);
                            if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcd5arorrf8jkg, 578, 27);
                            gm_b16_8 t96;
                            GM_ST(void *, (uint8_t *)&t96, (void *)(((uint8_t *)t95 + (uintptr_t)0 * 16)));
                            GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                            gm_b16_8 t97 = t96;
                            gm_tftb1odc739t5 t98 = gm_f_extend_vogsd72(t90, t97);
                            if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 1, gm_srcd5arorrf8jkg, 578, 13);
                            gm_b0_1 t99 = GM_LD(gm_b0_1, (((uint8_t *)&t98) + 1));
                        }
                        {
                            gm_b16_8 t100;
                            void * t102 = gm_l0;
                            void * t103 = (void *)(((uint8_t *)t102) + 8);
                            uint64_t t104 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t105 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                            if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcd5arorrf8jkg, 580, 47);
                            gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t104 * 4));
                            uint8_t t107 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t103, t106);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            if (!t107) goto ie101;
                            gm_b16_8 t109;
                            GM_ST(void *, (uint8_t *)&t109, (void *)(gm_s205esok2u8pk));
                            GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(3));
                            gm_b16_8 t110 = t109;
                            t100 = t110;
                            goto ix101;
ie101:;
                            gm_b16_8 t112 = gm_l13;
                            gm_tnpifv20irnph t113;
                            GM_ST(gm_b16_8, ((uint8_t *)&t113 + 0), t112);
                            gm_tabb710d9mq2e t114;
                            gm_f_format_3g143bv((void *)&t114, (void *)NULL, cx, NULL, t113);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 8, gm_srcd5arorrf8jkg, 580, 79);
                            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
                            gm_b16_8 t116 = t115;
                            gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
                            t100 = t117;
ix101:;
                            gm_l16 = t100;
                        }
                        {
                            gm_f_list_epvducg((void *)&F.l17, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                        }
                        {
                        }
                        {
                            void * t118 = (void *)((uint8_t *)&F.l17);
                            {
                                gm_b16_8 t120;
                                GM_ST(void *, (uint8_t *)&t120, (void *)(gm_s04qus0luoga5));
                                GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(4));
                                gm_b16_8 t121;
                                GM_ST(void *, (uint8_t *)&t121, (void *)(gm_s10iv34fu7qe7));
                                GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(2));
                                gm_b16_8 t122;
                                GM_ST(void *, (uint8_t *)&t122, (void *)(gm_suqchf5vh2hqp));
                                GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(4));
                                gm_b16_8 t123 = gm_l16;
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdmi4fk9nhqa7 *)((uint8_t *)&F.l18)) + 0, t120);
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdmi4fk9nhqa7 *)((uint8_t *)&F.l18)) + 16, t121);
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdmi4fk9nhqa7 *)((uint8_t *)&F.l18)) + 32, t122);
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdmi4fk9nhqa7 *)((uint8_t *)&F.l18)) + 48, t123);
                            }
                            void * t124 = ((uint8_t *)&F.l18);
                            if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcd5arorrf8jkg, 582, 25);
                            gm_b16_8 t125;
                            GM_ST(void *, (uint8_t *)&t125, (void *)(((uint8_t *)t124 + (uintptr_t)0 * 16)));
                            GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                            gm_b16_8 t126 = t125;
                            gm_tftb1odc739t5 t127 = gm_f_extend_vogsd72(t118, t126);
                            if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 1, gm_srcd5arorrf8jkg, 582, 13);
                            gm_b0_1 t128 = GM_LD(gm_b0_1, (((uint8_t *)&t127) + 1));
                        }
                        {
                            {
                                gm_l20 = ((uint64_t)1ULL);
                            }
                            {
                                uint64_t t130 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                                gm_l19 = t130;
                            }
                            {
                                for (;;) {
                                    uint64_t t132 = gm_l20;
                                    uint64_t t133 = gm_l19;
                                    uint8_t t134 = (t132 < t133);
                                    if (!t134) goto lb131;
                                    {
                                        uint64_t t137 = gm_l20;
                                        uint64_t t140 = GM_LD(uint64_t, ((uint8_t *)&t137));
                                        gm_l21 = t140;
                                        {
                                            void * t142 = (void *)((uint8_t *)&F.l14);
                                            gm_t3877pk7lv748 t143;
                                            void * t144 = gm_l0;
                                            void * t145 = (void *)(((uint8_t *)t144) + 8);
                                            uint64_t t146 = gm_l21;
                                            uint64_t t147 = (uintptr_t)t146;
                                            uint64_t t148 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                                            if (t147 >= (uintptr_t)(t148)) gm_trap_index(t147, t148, gm_srcd5arorrf8jkg, 584, 65);
                                            gm_t88rcucg1b7uk t149 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t147 * 4));
                                            gm_b16_8 t150 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t145, t149);
                                            if (gm_unw.kind) {
                                                goto gm_ll135;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t143 + 0, t150);
                                            uint64_t t151 = gm_l21;
                                            GM_ST(uint64_t, (uint8_t *)&t143 + 16, t151);
                                            gm_tabb710d9mq2e t152;
                                            gm_f_format_eq87866((void *)&t152, (void *)NULL, cx, NULL, &t143);
                                            if (gm_unw.kind) {
                                                goto gm_ll135;
                                            }
                                            if (((((uint8_t *)&t152))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t152) + 8, gm_srcd5arorrf8jkg, 584, 29);
                                            gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&t152) + 8));
                                            gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&t153));
                                            gm_tftb1odc739t5 t155 = gm_f_push_3ef746b(t142, t154);
                                            if (((((uint8_t *)&t155))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t155) + 1, gm_srcd5arorrf8jkg, 584, 17);
                                            gm_b0_1 t156 = GM_LD(gm_b0_1, (((uint8_t *)&t155) + 1));
                                        }
                                        {
                                            void * t157 = (void *)((uint8_t *)&F.l17);
                                            uint64_t t158 = gm_l21;
                                            gm_t4ce0jd3tdro4 t159;
                                            GM_ST(uint64_t, ((uint8_t *)&t159 + 0), t158);
                                            gm_tabb710d9mq2e t160;
                                            gm_f_format_l05am3t((void *)&t160, (void *)NULL, cx, NULL, t159);
                                            if (gm_unw.kind) {
                                                goto gm_ll135;
                                            }
                                            if (((((uint8_t *)&t160))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t160) + 8, gm_srcd5arorrf8jkg, 585, 27);
                                            gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&t160) + 8));
                                            gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                                            gm_tftb1odc739t5 t163 = gm_f_push_3ef746b(t157, t162);
                                            if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 1, gm_srcd5arorrf8jkg, 585, 17);
                                            gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t163) + 1));
                                        }
                                        goto me138;
mn139:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 583, 13);
me138:;
                                    }
lc131:;
                                    {
                                        uint64_t t166 = gm_l20;
                                        uint64_t r167; bool o168 = __builtin_add_overflow(t166, ((uint64_t)1ULL), &r167);
                                        uint64_t t169 = r167;
                                        gm_l20 = t169;
                                    }
                                }
gm_ll135:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb131; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc131; }
                                }
                                goto gm_ll28;
lb131:;
                            }
                        }
                        {
                            gm_t3877pk7lv748 t170;
                            void * t171 = gm_l0;
                            uint32_t t172 = gm_l1;
                            gm_b16_8 t173 = gm_f_decl_name_fode8m9((void *)NULL, cx, NULL, t171, t172);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            uint64_t t174 = GM_LD(uintptr_t, ((uint8_t *)&t173) + 8);
                            void * t175 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t173)));
                            gm_b16_8 t176;
                            GM_ST(void *, (uint8_t *)&t176, (void *)(gm_scj1kuqp0242l));
                            GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(5));
                            uint64_t t177 = GM_LD(uintptr_t, ((uint8_t *)&t176) + 8);
                            uint64_t t178 = (uintptr_t)t177;
                            if (t178 > (uintptr_t)t174 || (uintptr_t)t174 > (uintptr_t)t174) gm_trap_slice(t178, (uintptr_t)t174, t174, gm_srcd5arorrf8jkg, 587, 46);
                            gm_b16_8 t179;
                            GM_ST(void *, (uint8_t *)&t179, (void *)(((uint8_t *)t175 + t178 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)((uintptr_t)t174 - t178));
                            GM_ST(gm_b16_8, (uint8_t *)&t170 + 0, t179);
                            uint64_t t180 = gm_l5;
                            GM_ST(uint64_t, (uint8_t *)&t170 + 16, t180);
                            gm_tabb710d9mq2e t181;
                            gm_f_format_dgfvefu((void *)&t181, (void *)NULL, cx, NULL, &t170);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 8, gm_srcd5arorrf8jkg, 587, 21);
                            gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&t181) + 8));
                            gm_l22 = t182;
                        }
                        {
                            gm_b16_8 t183;
                            GM_ST(void *, (uint8_t *)&t183, (void *)(gm_smbbaf3n1gml2));
                            GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)(7));
                            gm_l23 = t183;
                        }
                        {
                            void * t185 = gm_l0;
                            void * t186 = (void *)(((uint8_t *)t185) + 8);
                            void * t187 = gm_l7;
                            gm_t88rcucg1b7uk t188 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t187) + 64));
                            uint8_t t189 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t186, t188);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            if (!t189) goto ie184;
                            {
                                void * t191 = (void *)((uint8_t *)&F.l14);
                                gm_b16_8 t192;
                                GM_ST(void *, (uint8_t *)&t192, (void *)(gm_s4ql65vkv7h9p));
                                GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(12));
                                gm_tftb1odc739t5 t193 = gm_f_insert_v6fq3iv(t191, ((uint64_t)0ULL), t192);
                                if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 1, gm_srcd5arorrf8jkg, 591, 17);
                                gm_b0_1 t194 = GM_LD(gm_b0_1, (((uint8_t *)&t193) + 1));
                            }
                            {
                                void * t195 = (void *)((uint8_t *)&F.l17);
                                gm_b16_8 t196;
                                GM_ST(void *, (uint8_t *)&t196, (void *)(gm_sq9pusqm0kq2f));
                                GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(6));
                                gm_tftb1odc739t5 t197 = gm_f_insert_v6fq3iv(t195, ((uint64_t)0ULL), t196);
                                if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 1, gm_srcd5arorrf8jkg, 592, 17);
                                gm_b0_1 t198 = GM_LD(gm_b0_1, (((uint8_t *)&t197) + 1));
                            }
                            {
                                gm_b16_8 t199;
                                GM_ST(void *, (uint8_t *)&t199, (void *)(gm_sts5j6f5pkov0));
                                GM_ST(uintptr_t, (uint8_t *)&t199 + 8, (uintptr_t)(4));
                                gm_l8 = t199;
                            }
                            {
                                gm_b16_8 t200;
                                GM_ST(void *, (uint8_t *)&t200, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(0));
                                gm_l23 = t200;
                            }
                            goto ix184;
ie184:;
ix184:;
                        }
                        {
                            void * t201 = gm_l0;
                            void * t202 = (void *)(((uint8_t *)t201) + 1888);
                            gm_tfeab279c16rr t203;
                            gm_b16_8 t204 = gm_l8;
                            GM_ST(gm_b16_8, (uint8_t *)&t203 + 0, t204);
                            gm_b16_8 t205 = gm_l22;
                            GM_ST(gm_b16_8, (uint8_t *)&t203 + 16, t205);
                            gm_b16_8 t206 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                            gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&t206));
                            gm_b16_8 t208;
                            GM_ST(void *, (uint8_t *)&t208, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(2));
                            gm_tabb710d9mq2e t209;
                            gm_f_join_sfknljb((void *)&t209, (void *)NULL, cx, NULL, t207, t208);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            if (((((uint8_t *)&t209))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t209) + 8, gm_srcd5arorrf8jkg, 596, 62);
                            gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)&t209) + 8));
                            GM_ST(gm_b16_8, (uint8_t *)&t203 + 32, t210);
                            gm_tftb1odc739t5 t211 = gm_f_write_qqu2rl9(t202, &t203);
                            if (((((uint8_t *)&t211))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t211) + 1, gm_srcd5arorrf8jkg, 596, 13);
                            gm_b0_1 t212 = GM_LD(gm_b0_1, (((uint8_t *)&t211) + 1));
                        }
                        {
                            void * t213 = gm_l0;
                            void * t214 = (void *)(((uint8_t *)t213) + 1928);
                            gm_tif3elqk01bdl t215;
                            gm_b16_8 t216 = gm_l8;
                            GM_ST(gm_b16_8, (uint8_t *)&t215 + 0, t216);
                            gm_b16_8 t217 = gm_l22;
                            GM_ST(gm_b16_8, (uint8_t *)&t215 + 16, t217);
                            gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                            gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&t218));
                            gm_b16_8 t220;
                            GM_ST(void *, (uint8_t *)&t220, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t220 + 8, (uintptr_t)(2));
                            gm_tabb710d9mq2e t221;
                            gm_f_join_sfknljb((void *)&t221, (void *)NULL, cx, NULL, t219, t220);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            if (((((uint8_t *)&t221))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t221) + 8, gm_srcd5arorrf8jkg, 597, 77);
                            gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)&t221) + 8));
                            GM_ST(gm_b16_8, (uint8_t *)&t215 + 32, t222);
                            gm_b16_8 t223 = gm_l23;
                            GM_ST(gm_b16_8, (uint8_t *)&t215 + 48, t223);
                            void * t224 = gm_l0;
                            uint32_t t225 = gm_l6;
                            gm_b16_8 t226 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t224, t225);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t215 + 64, t226);
                            gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                            gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&t227));
                            gm_b16_8 t229;
                            GM_ST(void *, (uint8_t *)&t229, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(2));
                            gm_tabb710d9mq2e t230;
                            gm_f_join_sfknljb((void *)&t230, (void *)NULL, cx, NULL, t228, t229);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            if (((((uint8_t *)&t230))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t230) + 8, gm_srcd5arorrf8jkg, 597, 130);
                            gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&t230) + 8));
                            GM_ST(gm_b16_8, (uint8_t *)&t215 + 80, t231);
                            gm_tftb1odc739t5 t232 = gm_f_write_qph5tm9(t214, &t215);
                            if (((((uint8_t *)&t232))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t232) + 1, gm_srcd5arorrf8jkg, 597, 13);
                            gm_b0_1 t233 = GM_LD(gm_b0_1, (((uint8_t *)&t232) + 1));
                        }
                        {
                            void * t234 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t235 = gm_l22;
                            gm_tnpifv20irnph t236;
                            GM_ST(gm_b16_8, ((uint8_t *)&t236 + 0), t235);
                            gm_tabb710d9mq2e t237;
                            gm_f_format_2g08abe((void *)&t237, (void *)NULL, cx, NULL, t236);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            if (((((uint8_t *)&t237))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t237) + 8, gm_srcd5arorrf8jkg, 598, 24);
                            gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&t237) + 8));
                            gm_b16_8 t239 = GM_LD(gm_b16_8, ((uint8_t *)&t238));
                            gm_tftb1odc739t5 t240 = gm_f_push_3ef746b(t234, t239);
                            if (((((uint8_t *)&t240))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t240) + 1, gm_srcd5arorrf8jkg, 598, 13);
                            gm_b0_1 t241 = GM_LD(gm_b0_1, (((uint8_t *)&t240) + 1));
                        }
                        goto me36;
mn37:;
                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 570, 9);
me36:;
                    }
lc24:;
                    {
                        uint64_t t243 = gm_l4;
                        uint64_t r244; bool o245 = __builtin_add_overflow(t243, ((uint64_t)1ULL), &r244);
                        uint64_t t246 = r244;
                        gm_l4 = t246;
                    }
                }
gm_ll28:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb24; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc24; }
                }
                goto gm_lad;
lb24:;
            }
        }
        {
            void * t247 = gm_l0;
            void * t248 = (void *)(((uint8_t *)t247) + 3080);
            gm_t6j6smj7qoctu t249;
            void * t250 = gm_l0;
            uint32_t t251 = gm_l1;
            gm_b16_8 t252 = gm_f_decl_name_fode8m9((void *)NULL, cx, NULL, t250, t251);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t249 + 0, t252);
            gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
            gm_b16_8 t255;
            GM_ST(void *, (uint8_t *)&t255, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t255 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t256;
            gm_f_join_sfknljb((void *)&t256, (void *)NULL, cx, NULL, t254, t255);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t256))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t256) + 8, gm_srcd5arorrf8jkg, 600, 87);
            gm_b16_8 t257 = GM_LD(gm_b16_8, (((uint8_t *)&t256) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t249 + 16, t257);
            gm_tftb1odc739t5 t258 = gm_f_write_b3knhd6(t248, &t249);
            if (((((uint8_t *)&t258))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t258) + 1, gm_srcd5arorrf8jkg, 600, 9);
            gm_b0_1 t259 = GM_LD(gm_b0_1, (((uint8_t *)&t258) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t260 = gm_l0;
        uint32_t t261 = gm_l1;
        gm_b16_8 t262 = gm_f_decl_name_fode8m9((void *)NULL, cx, NULL, t260, t261);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t262);
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
inline __attribute__((aligned(4))) void gm_f_drain_leoibma(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_drain_leoibma F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    gm_tcc1rq4n1avb2 gm_l2; /* c */
    GM_STACK_CHECK(&F, 372u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb2;
            {
                void * t6 = gm_l0;
                void * t7 = (void *)(((uint8_t *)t6) + 2272);
                gm_t9805qtonbsag t8 = gm_f_pop_570g79n(t7);
                if (!((((uint8_t *)&t8))[4] != 0)) goto ie5;
                uint32_t t9 = GM_LD(uint32_t, ((uint8_t *)&t8));
                gm_l1 = t9;
                {
                    void * t11 = gm_l0;
                    void * t12 = gm_l0;
                    uint32_t t13 = gm_l1;
                    gm_tgqgdb97ka5kt t14 = gm_f_owner_of_cirm596((void *)NULL, cx, NULL, t12, t13);
                    if (gm_unw.kind) {
                        goto gm_ll3;
                    }
                    gm_b0_1 t15 = gm_f_mark_owner_uhhhf9f((void *)NULL, cx, NULL, t11, t14);
                    if (gm_unw.kind) {
                        goto gm_ll3;
                    }
                }
                {
                    void * t16 = gm_l0;
                    uint32_t t17 = gm_l1;
                    gm_t9805qtonbsag t18;
                    memset(&t18, 0, sizeof t18);
                    gm_tqgtd5gtgj65s t19;
                    gm_f_gen_fn_2fqvb90((void *)&t19, (void *)NULL, cx, NULL, t16, t17, t18);
                    if (gm_unw.kind) {
                        goto gm_ll3;
                    }
                    if (((((uint8_t *)&t19))[0] != 0)) {
                        gm_tqgtd5gtgj65s t20;
                        GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
                        ((uint8_t *)&t20)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t20 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t19) + 8)));
                        F.ret = t20;
                        goto gm_fret;
                    }
                    gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 8));
                }
                goto ix5;
ie5:;
                void * t23 = gm_l0;
                void * t24 = (void *)(((uint8_t *)t23) + 2744);
                gm_t736djc66po0l t25 = gm_f_pop_168lbb8(t24);
                if (!((((uint8_t *)&t25))[8] != 0)) goto ie22;
                gm_tcc1rq4n1avb2 t26 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)&t25));
                gm_l2 = t26;
                {
                    void * t28 = gm_l0;
                    void * t29 = gm_l0;
                    uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
                    gm_tgqgdb97ka5kt t31 = gm_f_owner_of_cirm596((void *)NULL, cx, NULL, t29, t30);
                    if (gm_unw.kind) {
                        goto gm_ll3;
                    }
                    gm_b0_1 t32 = gm_f_mark_owner_uhhhf9f((void *)NULL, cx, NULL, t28, t31);
                    if (gm_unw.kind) {
                        goto gm_ll3;
                    }
                }
                {
                    void * t33 = gm_l0;
                    uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
                    uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
                    gm_t9805qtonbsag t36;
                    GM_ST(uint8_t, (uint8_t *)&t36 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t36 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t36), t35);
                    ((uint8_t *)&t36)[4] = 1;
                    gm_tqgtd5gtgj65s t37;
                    gm_f_gen_fn_2fqvb90((void *)&t37, (void *)NULL, cx, NULL, t33, t34, t36);
                    if (gm_unw.kind) {
                        goto gm_ll3;
                    }
                    if (((((uint8_t *)&t37))[0] != 0)) {
                        gm_tqgtd5gtgj65s t38;
                        GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                        ((uint8_t *)&t38)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t38 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t37) + 8)));
                        F.ret = t38;
                        goto gm_fret;
                    }
                    gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 8));
                }
                goto ix22;
ie22:;
                {
                    void * t41 = gm_l0;
                    gm_tgqgdb97ka5kt t42 = GM_LD(gm_tgqgdb97ka5kt, gm_ctvsbqjhdl3);
                    gm_b0_1 t43 = gm_f_mark_owner_uhhhf9f((void *)NULL, cx, NULL, t41, t42);
                    if (gm_unw.kind) {
                        goto gm_ll3;
                    }
                }
                {
                    gm_b0_1 t44;
                    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t44);
                    goto gm_fdone;
                }
ix22:;
ix5:;
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
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_effect_id_5588nqa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_effect_id_5588nqa F;
    void * gm_l0; /* g */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* key */
    uint32_t gm_l3; /* id */
    uint32_t gm_l4; /* n */
    GM_STACK_CHECK(&F, 327u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        void * t6 = gm_l1;
        gm_b16_8 t7 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t7;
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 1160);
        gm_b16_8 t11 = gm_l2;
        gm_t9805qtonbsag t12 = gm_f_get_m1knqqk(t10, t11);
        if (!((((uint8_t *)&t12))[4] != 0)) goto ie8;
        uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&t12));
        gm_l3 = t13;
        {
            uint32_t t15 = gm_l3;
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        gm_b16_8 t17 = gm_l2;
        uint64_t t18 = gm_f_hash64_apkb7bo(t17);
        uint64_t t19 = (uint64_t)(t18 & ((uint64_t)2147483647ULL));
        uint32_t t20 = (uint32_t)t19;
        gm_l4 = t20;
    }
    {
        for (;;) {
            void * t22 = gm_l0;
            void * t23 = (void *)(((uint8_t *)t22) + 1656);
            uint32_t t24 = gm_l4;
            gm_th2lqbo77pvsc t25;
            gm_f_get_gnmase9((void *)&t25, t23, t24);
            uint8_t t26;
            if (((((uint8_t *)&t25))[16] != 0)) goto mn28;
            t26 = ((uint8_t)0);
            goto me27;
mn28:;
            t26 = ((uint8_t)1);
            goto me27;
mn29:;
            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 487, 11);
me27:;
            if (!t26) goto lb21;
            {
                uint32_t t32 = gm_l4;
                uint32_t r33; bool o34 = __builtin_add_overflow(t32, ((uint32_t)1ULL), &r33);
                if (o34) gm_trap("integer overflow in `+`", gm_srcd5arorrf8jkg, 487, 52);
                uint32_t t35 = r33;
                uint32_t t36 = (uint32_t)(t35 & ((uint32_t)2147483647ULL));
                gm_l4 = t36;
            }
lc21:;
        }
lb21:;
    }
    {
        void * t37 = gm_l0;
        void * t38 = (void *)(((uint8_t *)t37) + 1160);
        gm_b16_8 t39 = gm_l2;
        uint32_t t40 = gm_l4;
        gm_tftb1odc739t5 t41 = gm_f_put_074448v(t38, t39, t40);
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcd5arorrf8jkg, 488, 5);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
    }
    {
        void * t43 = gm_l0;
        void * t44 = (void *)(((uint8_t *)t43) + 1656);
        uint32_t t45 = gm_l4;
        gm_b16_8 t46 = gm_l2;
        gm_tftb1odc739t5 t47 = gm_f_put_po09vud(t44, t45, t46);
        if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srcd5arorrf8jkg, 489, 5);
        gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
    }
    {
        uint32_t t49 = gm_l4;
        uint8_t t50 = 0;
        F.ret = t49;
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
__attribute__((aligned(4))) uint8_t gm_f_expr_may_exit_qfnn2ei(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9805qtonbsag a2) {
    struct gm_fr_expr_may_exit_qfnn2ei F;
    void * gm_l1; /* e */
    gm_teo9d4mncjfdu gm_l3; /* b */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l5; /* levels */
    uint32_t gm_l6; /* levels */
    gm_t083kd7b1kkag gm_l7; /* t */
    uint8_t gm_l9; /* may */
    uint32_t gm_l10; /* f */
    gm_tiilmfd16v3fc gm_l11; /* eff */
    uint32_t gm_l12; /* op */
    uint32_t gm_l13; /* s */
    gm_tf9f5m02f56gm gm_l14; /* key */
    gm_b16_8 gm_l15; /* clauses */
    GM_STACK_CHECK(&F, 574u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    {
        void * t2 = gm_l1;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma13; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma13; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma13; case ((uint8_t)27ULL): goto ma13; case ((uint8_t)28ULL): goto ma13; case ((uint8_t)29ULL): goto ma13; case ((uint8_t)30ULL): goto ma13; case ((uint8_t)31ULL): goto ma13; case ((uint8_t)32ULL): goto ma13; case ((uint8_t)33ULL): goto ma10; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma13; case ((uint8_t)36ULL): goto ma13; case ((uint8_t)37ULL): goto ma13; case ((uint8_t)38ULL): goto ma13; case ((uint8_t)39ULL): goto ma13; case ((uint8_t)40ULL): goto ma13; case ((uint8_t)41ULL): goto ma13; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma13; case ((uint8_t)44ULL): goto ma13; case ((uint8_t)45ULL): goto ma13; case ((uint8_t)46ULL): goto ma13; case ((uint8_t)47ULL): goto ma13; case ((uint8_t)48ULL): goto ma13; case ((uint8_t)49ULL): goto ma4; case ((uint8_t)50ULL): goto ma5; case ((uint8_t)51ULL): goto ma7; case ((uint8_t)52ULL): goto ma13; case ((uint8_t)53ULL): goto ma9; case ((uint8_t)54ULL): goto ma9; case ((uint8_t)55ULL): goto ma11; case ((uint8_t)56ULL): goto ma13; case ((uint8_t)57ULL): goto ma13; case ((uint8_t)58ULL): goto ma9; default: goto ma13; }
ma4:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)49ULL)) goto mn16;
        gm_teo9d4mncjfdu t18 = GM_LD(gm_teo9d4mncjfdu, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t18;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)&gm_l3) + 0) + 0));
        if (t21 != ((uint8_t)1ULL)) goto mn20;
        uint8_t t22 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me19;
mn20:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)&gm_l3) + 0) + 0));
        if (t24 != ((uint8_t)0ULL)) goto mn23;
        uint32_t t25 = GM_LD(uint32_t, (((((uint8_t *)&gm_l3) + 0) + 4) + 0));
        gm_l4 = t25;
        uint32_t t27 = gm_l4;
        uint8_t t28 = (t27 > ((uint32_t)0ULL));
        if (!t28) goto ie26;
        {
            uint8_t t30 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
        goto me19;
mn23:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 793, 22);
me19:;
        goto me3;
mn16:;
ma5:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t32 != ((uint8_t)50ULL)) goto mn31;
        uint8_t t33 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        if (t33 != ((uint8_t)0ULL)) goto mn31;
        uint32_t t34 = GM_LD(uint32_t, ((((((uint8_t *)t2) + 8) + 8) + 4) + 0));
        gm_l5 = t34;
        uint32_t t36 = gm_l5;
        uint8_t t37 = (t36 > ((uint32_t)0ULL));
        if (!t37) goto ie35;
        {
            uint8_t t39 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix35;
ie35:;
ix35:;
        goto me3;
mn31:;
ma6:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t41 != ((uint8_t)50ULL)) goto mn40;
        uint8_t t42 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        if (t42 != ((uint8_t)1ULL)) goto mn40;
        goto me3;
mn40:;
ma7:;
        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t45 != ((uint8_t)51ULL)) goto mn44;
        uint32_t t46 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l6 = t46;
        uint32_t t48 = gm_l6;
        uint8_t t49 = (t48 > ((uint32_t)0ULL));
        if (!t49) goto ie47;
        {
            uint8_t t51 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix47;
ie47:;
ix47:;
        goto me3;
mn44:;
ma8:;
        uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t53 != ((uint8_t)42ULL)) goto mn52;
        gm_t083kd7b1kkag t54 = GM_LD(gm_t083kd7b1kkag, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t54;
        uint32_t t56 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 16));
        uint8_t t57 = (t56 > ((uint32_t)0ULL));
        if (!t57) goto ie55;
        {
            uint8_t t59 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix55;
ie55:;
ix55:;
        goto me3;
mn52:;
ma9:;
        uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t63 != ((uint8_t)58ULL)) goto pa62;
        goto po61;
pa62:;
        uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t65 != ((uint8_t)53ULL)) goto pa64;
        goto po61;
pa64:;
        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t67 != ((uint8_t)54ULL)) goto pa66;
        goto po61;
pa66:;
        goto mn60;
po61:;
        uint8_t t68 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn60:;
ma10:;
        uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t70 != ((uint8_t)33ULL)) goto mn69;
        uint8_t t71 = GM_LD(uint8_t, ((((((uint8_t *)t2) + 8) + 8) + 0) + 0));
        if (t71 != ((uint8_t)73ULL)) goto mn69;
        uint8_t t72 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn69:;
ma11:;
        uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t74 != ((uint8_t)55ULL)) goto mn73;
        uint8_t t75 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn73:;
ma12:;
        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t77 != ((uint8_t)34ULL)) goto mn76;
        gm_tsth9qu1dv0g3 t78 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t2) + 8) + 8) + 0));
        F.l8 = t78;
        {
            gm_l9 = ((uint8_t)0);
        }
        {
            uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
            switch (t85) { case ((uint8_t)0ULL): goto ma81; case ((uint8_t)1ULL): goto ma83; case ((uint8_t)2ULL): goto ma82; default: goto ma84; }
ma81:;
            uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
            if (t87 != ((uint8_t)0ULL)) goto mn86;
            uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)&F.l8) + 8));
            gm_l10 = t88;
            {
                void * t90 = F.l0;
                uint32_t t91 = gm_l10;
                uint8_t t92 = gm_f_may_unwind_irl2ktm((void *)NULL, cx, NULL, t90, t91);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t92;
            }
            goto me80;
mn86:;
ma82:;
            uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
            if (t94 != ((uint8_t)2ULL)) goto mn93;
            {
                gm_l9 = ((uint8_t)1);
            }
            goto me80;
mn93:;
ma83:;
            uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
            if (t97 != ((uint8_t)1ULL)) goto mn96;
            gm_tiilmfd16v3fc t98 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)&F.l8) + 8) + 0));
            gm_l11 = t98;
            uint32_t t99 = GM_LD(uint32_t, ((((uint8_t *)&F.l8) + 8) + 24));
            gm_l12 = t99;
            {
                gm_l9 = ((uint8_t)1);
            }
            {
                if (!((((uint8_t *)&F.l2))[4] != 0)) goto ie101;
                uint32_t t102 = GM_LD(uint32_t, ((uint8_t *)&F.l2));
                gm_l13 = t102;
                {
                    void * t105 = F.l0;
                    void * t106 = GM_LD(void *, (((uint8_t *)t105) + 0));
                    uint32_t t107 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 0));
                    uint32_t t108 = gm_l12;
                    uint8_t t109 = gm_f_is_ctl_oclllo3(t106, t107, t108);
                    uint8_t t110 = (uint8_t)!t109;
                    if (!t110) goto ie104;
                    {
                        void * t112 = F.l0;
                        uint32_t t113 = gm_l13;
                        gm_tf9f5m02f56gm t114 = gm_f_spec_key_eljlge7(t112, t113);
                        gm_l14 = t114;
                    }
                    {
                        void * t115 = F.l0;
                        void * t116 = GM_LD(void *, (((uint8_t *)t115) + 0));
                        uint32_t t117 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 0));
                        void * t118 = gm_f_handler_lit_b02boff(t116, t117);
                        gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)t118) + 24));
                        gm_l15 = t119;
                    }
                    {
                        uint32_t t121 = gm_l12;
                        uint64_t t122 = (uint64_t)t121;
                        uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        uint8_t t124 = (t122 < t123);
                        if (!t124) goto ie120;
                        void * t125 = F.l0;
                        void * t126 = (void *)((uint8_t *)&gm_l11);
                        uint32_t t127 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t125, t126);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint32_t t128 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 4));
                        uint8_t t129 = ((t127 == t128));
                        if (!t129) goto ie120;
                        {
                            void * t131 = F.l0;
                            uint32_t t132 = gm_l12;
                            uint64_t t133 = (uint64_t)t132;
                            uint64_t t134 = (uintptr_t)t133;
                            uint64_t t135 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                            if (t134 >= (uintptr_t)(t135)) gm_trap_index(t134, t135, gm_srcd5arorrf8jkg, 819, 57);
                            uint32_t t136 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t134 * 4));
                            gm_t9805qtonbsag t137 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l14) + 8));
                            uint8_t t138 = gm_f_clause_may_exit_7lc6flj((void *)NULL, cx, NULL, t131, t136, t137);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l9 = t138;
                        }
                        goto ix120;
ie120:;
ix120:;
                    }
                    goto ix104;
ie104:;
ix104:;
                }
                goto ix101;
ie101:;
ix101:;
            }
            goto me80;
mn96:;
ma84:;
            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 809, 13);
me80:;
        }
        {
            uint8_t t140 = gm_l9;
            if (!t140) goto ie139;
            {
                uint8_t t142 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix139;
ie139:;
ix139:;
        }
        goto me3;
mn76:;
ma13:;
        goto me3;
mn143:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 792, 5);
me3:;
    }
    {
        void * t145 = gm_l1;
        gm_closure t146 = ((gm_closure){(void *)gm_f_fn_93mqgd8, (void *)&F.h, cx});
        uint8_t t147 = gm_f_any_child_ffc8h1k(t145, t146);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t148 = 0;
        F.ret = t147;
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_93mqgd8(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_foreign_decl_rld7m78(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_foreign_decl_rld7m78 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* def */
    gm_b16_8 gm_l3; /* lib */
    gm_b16_8 gm_l4; /* link_name */
    uint8_t gm_l5; /* variadic */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* p */
    gm_b16_8 gm_l10; /* ret */
    GM_STACK_CHECK(&F, 1243u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
            uint32_t t5 = gm_l1;
            void * t6 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t4, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l2 = t6;
        }
        {
            void * t8 = gm_l2;
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 96) + 0));
            if (t9 != ((uint8_t)3ULL)) goto ie7;
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((((uint8_t *)t8) + 96) + 8) + 0));
            gm_l3 = t10;
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((((uint8_t *)t8) + 96) + 8) + 16));
            gm_l4 = t11;
            uint8_t t12 = GM_LD(uint8_t, (((((uint8_t *)t8) + 96) + 8) + 32));
            gm_l5 = t12;
            {
                gm_b16_8 t15 = gm_l3;
                gm_t6j6smj7qoctu t16;
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s2gnpj7k4l22n));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(1));
                GM_ST(gm_b16_8, (uint8_t *)&t16 + 0, t17);
                gm_b16_8 t18;
                GM_ST(void *, (uint8_t *)&t18, (void *)(gm_strca8t49bpqu));
                GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
                GM_ST(gm_b16_8, (uint8_t *)&t16 + 16, t18);
                uint8_t t19 = 0;
                if (gm_eq0iqiubfm7e(((uint8_t *)&t16) + 0, ((uint8_t *)&t15))) t19 = 1;
                if (gm_eq0iqiubfm7e(((uint8_t *)&t16) + 16, ((uint8_t *)&t15))) t19 = 1;
                uint8_t t20 = (uint8_t)(!t19);
                if (!t20) goto ie14;
                gm_b16_8 t21 = gm_l3;
                void * t22 = gm_l0;
                gm_b16_8 t23 = GM_LD(gm_b16_8, ((((uint8_t *)t22) + 3240) + 0));
                uint8_t t24 = 0;
                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t23) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t23))) + i * 16, ((uint8_t *)&t21))) { t24 = 1; break; }
                uint8_t t25 = (uint8_t)(!t24);
                if (!t25) goto ie14;
                {
                    void * t27 = gm_l0;
                    void * t28 = (void *)(((uint8_t *)t27) + 3240);
                    gm_b16_8 t29 = gm_l3;
                    gm_tftb1odc739t5 t30 = gm_f_push_3ef746b(t28, t29);
                    if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcd5arorrf8jkg, 898, 60);
                    gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                }
                goto ix14;
ie14:;
ix14:;
            }
            {
                gm_f_list_epvducg((void *)&F.l6, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    void * t33 = gm_l2;
                    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t33) + 32));
                    gm_l7 = t34;
                }
                {
                    gm_l8 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t36 = gm_l8;
                        uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t38 = (t36 < t37);
                        if (!t38) goto lb35;
                        {
                            uint64_t t41 = gm_l8;
                            uint64_t t42 = (uintptr_t)t41;
                            uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            uint32_t t44 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t42 * 4));
                            uint32_t t47 = GM_LD(uint32_t, ((uint8_t *)&t44));
                            gm_l9 = t47;
                            {
                                void * t49 = (void *)((uint8_t *)&F.l6);
                                void * t50 = gm_l0;
                                uint32_t t51 = gm_l9;
                                uint64_t t52 = (uint64_t)t51;
                                uint64_t t53 = (uintptr_t)t52;
                                void * t54 = gm_l2;
                                uint64_t t55 = GM_LD(uintptr_t, (((uint8_t *)t54) + 48) + 8);
                                if (t53 >= (uintptr_t)(t55)) gm_trap_index(t53, t55, gm_srcd5arorrf8jkg, 900, 56);
                                gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t54) + 48)) + t53 * 40) + 16));
                                void * t57 = gm_l2;
                                gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t57) + 16));
                                gm_t5o0tpv5jka2l t59;
                                gm_f_c_abi_type_nu2c2tm((void *)&t59, (void *)NULL, cx, NULL, t50, t56, t58);
                                if (gm_unw.kind) {
                                    goto gm_ll39;
                                }
                                if (((((uint8_t *)&t59))[0] != 0)) {
                                    gm_tqgtd5gtgj65s t60;
                                    GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
                                    ((uint8_t *)&t60)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t60 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t59) + 8)));
                                    F.ret = t60;
                                    goto gm_fret;
                                }
                                gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&t59) + 8));
                                gm_tftb1odc739t5 t62 = gm_f_push_3ef746b(t49, t61);
                                if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srcd5arorrf8jkg, 900, 27);
                                gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
                            }
                            goto me45;
mn46:;
                            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 900, 5);
me45:;
                        }
lc35:;
                        {
                            uint64_t t65 = gm_l8;
                            uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                            uint64_t t68 = r66;
                            gm_l8 = t68;
                        }
                    }
gm_ll39:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb35; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc35; }
                    }
                    goto gm_lad;
lb35:;
                }
            }
            {
                uint8_t t70 = gm_l5;
                if (!t70) goto ie69;
                {
                    void * t72 = (void *)((uint8_t *)&F.l6);
                    gm_b16_8 t73;
                    GM_ST(void *, (uint8_t *)&t73, (void *)(gm_soqt3jnmovq5l));
                    GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(3));
                    gm_tftb1odc739t5 t74 = gm_f_push_3ef746b(t72, t73);
                    if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 1, gm_srcd5arorrf8jkg, 901, 19);
                    gm_b0_1 t75 = GM_LD(gm_b0_1, (((uint8_t *)&t74) + 1));
                }
                goto ix69;
ie69:;
ix69:;
            }
            {
                uint64_t t77 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                uint8_t t78 = ((t77 == ((uint64_t)0ULL)));
                if (!t78) goto ie76;
                {
                    void * t80 = (void *)((uint8_t *)&F.l6);
                    gm_b16_8 t81;
                    GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sts5j6f5pkov0));
                    GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(4));
                    gm_tftb1odc739t5 t82 = gm_f_push_3ef746b(t80, t81);
                    if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_srcd5arorrf8jkg, 902, 32);
                    gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
                }
                goto ix76;
ie76:;
ix76:;
            }
            {
                gm_b16_8 t84;
                void * t86 = gm_l0;
                void * t87 = (void *)(((uint8_t *)t86) + 8);
                void * t88 = gm_l2;
                gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t88) + 64));
                uint8_t t90 = gm_f_is_never_in5ftma(t87, GM_CONV(gm_csguankko4lmhd, t89));
                if (!t90) goto ie85;
                gm_b16_8 t92;
                GM_ST(void *, (uint8_t *)&t92, (void *)(gm_shlk3g0tk7m4f));
                GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(30));
                gm_b16_8 t93 = t92;
                t84 = t93;
                goto ix85;
ie85:;
                gm_b16_8 t94;
                void * t96 = gm_l0;
                void * t97 = (void *)(((uint8_t *)t96) + 8);
                void * t98 = gm_l2;
                gm_t88rcucg1b7uk t99 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t98) + 64));
                uint8_t t100 = gm_f_is_unit_17uennd(t97, GM_CONV(gm_csguankko4lmhd, t99));
                if (!t100) goto ie95;
                gm_b16_8 t102;
                GM_ST(void *, (uint8_t *)&t102, (void *)(gm_sts5j6f5pkov0));
                GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(4));
                gm_b16_8 t103 = t102;
                t94 = t103;
                goto ix95;
ie95:;
                void * t105 = gm_l0;
                void * t106 = gm_l2;
                gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t106) + 64));
                void * t108 = gm_l2;
                gm_tl874t2atnlh3 t109 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t108) + 16));
                gm_t5o0tpv5jka2l t110;
                gm_f_c_abi_type_nu2c2tm((void *)&t110, (void *)NULL, cx, NULL, t105, t107, t109);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t110))[0] != 0)) {
                    gm_tqgtd5gtgj65s t111;
                    GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                    ((uint8_t *)&t111)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t111 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t110) + 8)));
                    F.ret = t111;
                    goto gm_fret;
                }
                gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t110) + 8));
                gm_b16_8 t113 = t112;
                t94 = t113;
ix95:;
                t84 = t94;
ix85:;
                gm_l10 = t84;
            }
            {
                void * t114 = gm_l0;
                void * t115 = (void *)(((uint8_t *)t114) + 1888);
                gm_tud7oovfvnf43 t116;
                void * t117 = gm_l2;
                uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)t117) + 144));
                gm_b16_8 t119 = gm_f_conv_attr_m0b89pc(t118);
                GM_ST(gm_b16_8, (uint8_t *)&t116 + 0, t119);
                gm_b16_8 t120 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t116 + 16, t120);
                void * t121 = gm_l0;
                uint32_t t122 = gm_l1;
                gm_b16_8 t123 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t121, t122);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t116 + 32, t123);
                gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
                gm_b16_8 t126;
                GM_ST(void *, (uint8_t *)&t126, (void *)(gm_sg5gdnrith2i6));
                GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(2));
                gm_tabb710d9mq2e t127;
                gm_f_join_sfknljb((void *)&t127, (void *)NULL, cx, NULL, t125, t126);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 8, gm_srcd5arorrf8jkg, 910, 106);
                gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&t127) + 8));
                GM_ST(gm_b16_8, (uint8_t *)&t116 + 48, t128);
                gm_b16_8 t129 = gm_l4;
                gm_b16_8 t130 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t129);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t116 + 64, t130);
                gm_tftb1odc739t5 t131 = gm_f_write_u3br5ci(t115, &t116);
                if (((((uint8_t *)&t131))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t131) + 1, gm_srcd5arorrf8jkg, 910, 5);
                gm_b0_1 t132 = GM_LD(gm_b0_1, (((uint8_t *)&t131) + 1));
            }
            goto ix7;
ie7:;
            {
                gm_trap("reached `@unreachable()`", gm_srcd5arorrf8jkg, 897, 57);
            }
ix7:;
        }
    }
    gm_b0_1 t134;
    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t134);
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
inline __attribute__((aligned(4))) uint8_t gm_f_frame_done_014nc4i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_frame_done_014nc4i F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* done */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        gm_l2 = t3;
    }
    {
        for (;;) {
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 2352) + 0) + 8);
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t6 <= t7);
            if (!t8) goto lb4;
            {
                void * t11 = gm_l0;
                void * t12 = (void *)(((uint8_t *)t11) + 2352);
                gm_tftb1odc739t5 t13 = gm_f_push_vd1290p(t12, ((uint8_t)0));
                if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcd5arorrf8jkg, 475, 42);
                gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
            }
lc4:;
        }
lb4:;
    }
    {
        uint64_t t15 = gm_l2;
        uint64_t t16 = (uintptr_t)t15;
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 2352) + 0) + 8);
        if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcd5arorrf8jkg, 476, 13);
        uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 2352) + 0)) + t16 * 1));
        gm_l3 = t19;
    }
    {
        uint64_t t20 = gm_l2;
        uint64_t t21 = (uintptr_t)t20;
        void * t22 = gm_l0;
        uint64_t t23 = GM_LD(uintptr_t, ((((uint8_t *)t22) + 2352) + 0) + 8);
        if (t21 >= (uintptr_t)(t23)) gm_trap_index(t21, t23, gm_srcd5arorrf8jkg, 477, 5);
        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t22) + 2352) + 0)) + t21 * 1), ((uint8_t)1));
    }
    {
        uint8_t t24 = gm_l3;
        uint8_t t25 = 0;
        F.ret = t24;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_gen_entry_hj4odev(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2) {
    struct gm_fr_gen_entry_hj4odev F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* entry */
    uint64_t gm_l2; /* n */
    gm_t88rcucg1b7uk gm_l3; /* ret */
    gm_b16_8 gm_l4; /* rc */
    gm_b16_8 gm_l6; /* hidden */
    gm_t88rcucg1b7uk gm_l7; /* err */
    gm_t88rcucg1b7uk gm_l8; /* ok */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* d */
    uint32_t gm_l11; /* id */
    gm_b16_8 gm_l12; /* name */
    GM_STACK_CHECK(&F, 1662u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint32_t t4 = gm_l1;
        void * t5 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 64));
        gm_l3 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = (void *)(((uint8_t *)t7) + 8);
        gm_t88rcucg1b7uk t9 = gm_l3;
        gm_b16_8 t10 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t10;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t11 = (void *)((uint8_t *)&F.l5);
        uint64_t t12 = gm_l2;
        gm_t4ce0jd3tdro4 t13;
        GM_ST(uint64_t, ((uint8_t *)&t13 + 0), t12);
        gm_tftb1odc739t5 t14 = gm_f_write_6rjcu4v(t11, t13);
        if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_srcd5arorrf8jkg, 721, 5);
        gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
    }
    {
        void * t17 = gm_l0;
        void * t18 = (void *)(((uint8_t *)t17) + 8);
        gm_t88rcucg1b7uk t19 = gm_l3;
        uint8_t t20 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t20) goto ie16;
        {
            gm_b16_8 t22;
            void * t24 = gm_l0;
            uint32_t t25 = gm_l1;
            uint8_t t26 = gm_f_is_plain_qr08i40((void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t26) goto ie23;
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(0));
            gm_b16_8 t29 = t28;
            t22 = t29;
            goto ix23;
ie23:;
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_s34o1ojfv2p3d));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(16));
            gm_b16_8 t32 = t31;
            t22 = t32;
ix23:;
            gm_l6 = t22;
        }
        {
            void * t33 = (void *)((uint8_t *)&F.l5);
            gm_tfeab279c16rr t34;
            gm_b16_8 t35 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t34 + 0, t35);
            void * t36 = gm_l0;
            uint32_t t37 = gm_l1;
            gm_b16_8 t38 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t34 + 16, t38);
            gm_b16_8 t39 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t34 + 32, t39);
            gm_tftb1odc739t5 t40 = gm_f_write_u8u1amh(t33, &t34);
            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srcd5arorrf8jkg, 724, 9);
            gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
        }
        goto ix16;
ie16:;
        void * t43 = gm_l0;
        uint32_t t44 = gm_l1;
        uint8_t t45 = gm_f_is_plain_qr08i40((void *)NULL, cx, NULL, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t45) goto ie42;
        {
            void * t48 = gm_l0;
            void * t49 = (void *)(((uint8_t *)t48) + 8);
            gm_t88rcucg1b7uk t50 = gm_l3;
            uint8_t t51 = gm_f_is_void_vs2outo(t49, GM_CONV(gm_csguankko4lmhd, t50));
            if (!t51) goto ie47;
            {
                void * t53 = (void *)((uint8_t *)&F.l5);
                gm_tfeab279c16rr t54;
                void * t55 = gm_l0;
                uint32_t t56 = gm_l1;
                gm_b16_8 t57 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t55, t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t54 + 0, t57);
                gm_b16_8 t58 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t54 + 16, t58);
                gm_b16_8 t59 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t54 + 32, t59);
                gm_tftb1odc739t5 t60 = gm_f_write_c5hlb1u(t53, &t54);
                if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_srcd5arorrf8jkg, 727, 13);
                gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
            }
            goto ix47;
ie47:;
            {
                void * t63 = (void *)((uint8_t *)&F.l5);
                gm_t6j6smj7qoctu t64;
                gm_b16_8 t65 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t64 + 0, t65);
                void * t66 = gm_l0;
                uint32_t t67 = gm_l1;
                gm_b16_8 t68 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t66, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t64 + 16, t68);
                gm_tftb1odc739t5 t69 = gm_f_write_8o29aab(t63, &t64);
                if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_srcd5arorrf8jkg, 729, 13);
                gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
            }
ix47:;
        }
        goto ix42;
ie42:;
        {
            void * t72 = (void *)((uint8_t *)&F.l5);
            gm_t6j6smj7qoctu t73;
            gm_b16_8 t74 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t73 + 0, t74);
            void * t75 = gm_l0;
            uint32_t t76 = gm_l1;
            gm_b16_8 t77 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t73 + 16, t77);
            gm_tftb1odc739t5 t78 = gm_f_write_usci3o4(t72, &t73);
            if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_srcd5arorrf8jkg, 732, 9);
            gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
        }
ix42:;
ix16:;
    }
    {
        void * t80 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t81;
        GM_ST(void *, (uint8_t *)&t81, (void *)(gm_scrjehcehrfpv));
        GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(14));
        gm_tftb1odc739t5 t82 = gm_f_extend_0cs85gs(t80, t81);
        if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_srcd5arorrf8jkg, 734, 5);
        gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
    }
    {
        void * t84 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t85;
        GM_ST(void *, (uint8_t *)&t85, (void *)(gm_s698sbcv3nmjk));
        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(68));
        gm_tftb1odc739t5 t86 = gm_f_extend_0cs85gs(t84, t85);
        if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srcd5arorrf8jkg, 735, 5);
        gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
    }
    {
        void * t88 = gm_l0;
        void * t89 = (void *)(((uint8_t *)t88) + 8);
        gm_t88rcucg1b7uk t90 = gm_l3;
        gm_t53at80pfdus3 t91;
        gm_f_kind_ghvav22((void *)&t91, t89, GM_CONV(gm_csguankko4lmhd, t90));
        uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&t91) + 0));
        if (t94 != ((uint8_t)14ULL)) goto mn93;
        gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t91) + 8) + 0));
        gm_l7 = t95;
        gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t91) + 8) + 4));
        gm_l8 = t96;
        {
            void * t98 = gm_l0;
            void * t99 = (void *)(((uint8_t *)t98) + 8);
            gm_t88rcucg1b7uk t100 = gm_l3;
            gm_b16_8 t101 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t101;
        }
        {
            void * t102 = gm_l0;
            void * t103 = (void *)(((uint8_t *)t102) + 8);
            gm_t88rcucg1b7uk t104 = gm_l7;
            gm_b16_8 t105 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t103, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t105;
        }
        {
            void * t106 = (void *)((uint8_t *)&F.l5);
            uint64_t t107 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t108 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
            if (t107 >= (uintptr_t)(t108)) gm_trap_index(t107, t108, gm_srcd5arorrf8jkg, 740, 58);
            uint64_t t109 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t107 * 8));
            gm_t4ce0jd3tdro4 t110;
            GM_ST(uint64_t, ((uint8_t *)&t110 + 0), t109);
            gm_tftb1odc739t5 t111 = gm_f_write_sf9mjak(t106, t110);
            if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 1, gm_srcd5arorrf8jkg, 740, 13);
            gm_b0_1 t112 = GM_LD(gm_b0_1, (((uint8_t *)&t111) + 1));
        }
        {
            void * t113 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t114;
            GM_ST(void *, (uint8_t *)&t114, (void *)(gm_sreie3avjjb9l));
            GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(63));
            gm_tftb1odc739t5 t115 = gm_f_extend_0cs85gs(t113, t114);
            if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 1, gm_srcd5arorrf8jkg, 741, 13);
            gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 1));
        }
        {
            void * t118 = gm_l0;
            void * t119 = (void *)(((uint8_t *)t118) + 8);
            gm_t88rcucg1b7uk t120 = gm_l7;
            gm_t53at80pfdus3 t121;
            gm_f_kind_ghvav22((void *)&t121, t119, GM_CONV(gm_csguankko4lmhd, t120));
            uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&t121) + 0));
            if (t122 != ((uint8_t)17ULL)) goto ie117;
            uint32_t t123 = GM_LD(uint32_t, ((((uint8_t *)&t121) + 8) + 0));
            gm_l11 = t123;
            void * t124 = gm_l0;
            void * t125 = GM_LD(void *, (((uint8_t *)t124) + 0));
            uint32_t t126 = gm_l11;
            void * t127 = gm_f_adt_mpqv8qe(t125, t126);
            uint8_t t128;
            uint8_t t131 = GM_LD(uint8_t, ((((uint8_t *)t127) + 24) + 0));
            if (t131 != ((uint8_t)1ULL)) goto mn130;
            t128 = ((uint8_t)1);
            goto me129;
mn130:;
            t128 = ((uint8_t)0);
            goto me129;
mn132:;
            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 743, 50);
me129:;
            if (!t128) goto ie117;
            {
                void * t134 = gm_l0;
                void * t135 = (void *)(((uint8_t *)t134) + 8);
                gm_t88rcucg1b7uk t136 = gm_l7;
                gm_b16_8 t137 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t135, t136);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t137;
            }
            {
                void * t138 = (void *)((uint8_t *)&F.l5);
                gm_t3877pk7lv748 t139;
                gm_b16_8 t140 = gm_l12;
                gm_b16_8 t141 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t140);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t139 + 0, t141);
                uint64_t t142 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                GM_ST(uint64_t, (uint8_t *)&t139 + 16, t142);
                gm_tftb1odc739t5 t143 = gm_f_write_9falfvm(t138, &t139);
                if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 1, gm_srcd5arorrf8jkg, 745, 17);
                gm_b0_1 t144 = GM_LD(gm_b0_1, (((uint8_t *)&t143) + 1));
            }
            goto ix117;
ie117:;
ix117:;
        }
        {
            void * t145 = (void *)((uint8_t *)&F.l5);
            gm_t3877pk7lv748 t146;
            gm_b16_8 t147 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t146 + 0, t147);
            uint64_t t148 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t149 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
            if (t148 >= (uintptr_t)(t149)) gm_trap_index(t148, t149, gm_srcd5arorrf8jkg, 747, 82);
            uint64_t t150 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t148 * 8));
            GM_ST(uint64_t, (uint8_t *)&t146 + 16, t150);
            gm_tftb1odc739t5 t151 = gm_f_write_kdsk4q9(t145, &t146);
            if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 1, gm_srcd5arorrf8jkg, 747, 13);
            gm_b0_1 t152 = GM_LD(gm_b0_1, (((uint8_t *)&t151) + 1));
        }
        {
            void * t153 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t154;
            GM_ST(void *, (uint8_t *)&t154, (void *)(gm_sucsarpa3psmp));
            GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(54));
            gm_tftb1odc739t5 t155 = gm_f_extend_0cs85gs(t153, t154);
            if (((((uint8_t *)&t155))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t155) + 1, gm_srcd5arorrf8jkg, 748, 13);
            gm_b0_1 t156 = GM_LD(gm_b0_1, (((uint8_t *)&t155) + 1));
        }
        {
            void * t157 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t158;
            GM_ST(void *, (uint8_t *)&t158, (void *)(gm_srjb0lv60gms5));
            GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(61));
            gm_tftb1odc739t5 t159 = gm_f_extend_0cs85gs(t157, t158);
            if (((((uint8_t *)&t159))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t159) + 1, gm_srcd5arorrf8jkg, 749, 13);
            gm_b0_1 t160 = GM_LD(gm_b0_1, (((uint8_t *)&t159) + 1));
        }
        {
            void * t162 = gm_l0;
            void * t163 = (void *)(((uint8_t *)t162) + 8);
            gm_t88rcucg1b7uk t164 = gm_l8;
            uint8_t t165 = gm_f_is_integer_eo6jkc5(t163, GM_CONV(gm_csguankko4lmhd, t164));
            if (!t165) goto ie161;
            {
                void * t167 = (void *)((uint8_t *)&F.l5);
                gm_t3877pk7lv748 t168;
                void * t169 = gm_l0;
                void * t170 = (void *)(((uint8_t *)t169) + 8);
                gm_t88rcucg1b7uk t171 = gm_l8;
                gm_b16_8 t172 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t170, t171);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t168 + 0, t172);
                uint64_t t173 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t174 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                if (t173 >= (uintptr_t)(t174)) gm_trap_index(t173, t174, gm_srcd5arorrf8jkg, 751, 112);
                uint64_t t175 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t173 * 8));
                GM_ST(uint64_t, (uint8_t *)&t168 + 16, t175);
                gm_tftb1odc739t5 t176 = gm_f_write_08b77ko(t167, &t168);
                if (((((uint8_t *)&t176))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t176) + 1, gm_srcd5arorrf8jkg, 751, 17);
                gm_b0_1 t177 = GM_LD(gm_b0_1, (((uint8_t *)&t176) + 1));
            }
            goto ix161;
ie161:;
            {
                void * t179 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t180;
                GM_ST(void *, (uint8_t *)&t180, (void *)(gm_sar77rocuptb5));
                GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(30));
                gm_tftb1odc739t5 t181 = gm_f_extend_0cs85gs(t179, t180);
                if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 1, gm_srcd5arorrf8jkg, 753, 17);
                gm_b0_1 t182 = GM_LD(gm_b0_1, (((uint8_t *)&t181) + 1));
            }
ix161:;
        }
        goto me92;
mn93:;
        {
            void * t186 = gm_l0;
            void * t187 = (void *)(((uint8_t *)t186) + 8);
            gm_t88rcucg1b7uk t188 = gm_l3;
            uint8_t t189 = gm_f_is_integer_eo6jkc5(t187, GM_CONV(gm_csguankko4lmhd, t188));
            if (!t189) goto ie185;
            {
                void * t191 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t192;
                GM_ST(void *, (uint8_t *)&t192, (void *)(gm_sasi3vvkcdvo0));
                GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(39));
                gm_tftb1odc739t5 t193 = gm_f_extend_0cs85gs(t191, t192);
                if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 1, gm_srcd5arorrf8jkg, 758, 17);
                gm_b0_1 t194 = GM_LD(gm_b0_1, (((uint8_t *)&t193) + 1));
            }
            goto ix185;
ie185:;
            {
                void * t196 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t197;
                GM_ST(void *, (uint8_t *)&t197, (void *)(gm_sekmbu83qkg9c));
                GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(43));
                gm_tftb1odc739t5 t198 = gm_f_extend_0cs85gs(t196, t197);
                if (((((uint8_t *)&t198))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t198) + 1, gm_srcd5arorrf8jkg, 760, 17);
                gm_b0_1 t199 = GM_LD(gm_b0_1, (((uint8_t *)&t198) + 1));
            }
ix185:;
        }
        goto me92;
mn183:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 736, 5);
me92:;
    }
    {
        void * t200 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t201;
        GM_ST(void *, (uint8_t *)&t201, (void *)(gm_sikpqtja8pqpv));
        GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t202 = gm_f_extend_0cs85gs(t200, t201);
        if (((((uint8_t *)&t202))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t202) + 1, gm_srcd5arorrf8jkg, 764, 5);
        gm_b0_1 t203 = GM_LD(gm_b0_1, (((uint8_t *)&t202) + 1));
    }
    {
        gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&t204));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t205);
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
__attribute__((aligned(4))) void gm_f_gen_exports_koai3gr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_gen_exports_koai3gr F;
    void * gm_l0; /* g */
    uint8_t gm_l2; /* wasi */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* k */
    gm_b16_8 gm_l6; /* sym */
    uint32_t gm_l7; /* f */
    void * gm_l8; /* def */
    gm_b16_8 gm_l9; /* cname */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    uint32_t gm_l16; /* p */
    gm_t88rcucg1b7uk gm_l17; /* t */
    gm_b16_8 gm_l18; /* pty */
    gm_b16_8 gm_l19; /* a */
    gm_b16_8 gm_l20; /* call */
    gm_b16_8 gm_l21; /* param_text */
    gm_b16_8 gm_l22; /* start */
    gm_b16_8 gm_l23; /* ret */
    gm_b16_8 gm_l24; /* value */
    gm_b16_8 gm_l25; /* body */
    gm_b16_8 gm_l26; /* result */
    gm_b16_8 gm_l27; /* cc */
    GM_STACK_CHECK(&F, 3953u);
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
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint8_t t4 = (((GM_LD(uintptr_t, ((((uint8_t *)t3) + 80) + 24) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)t3) + 80) + 24)), gm_svl13be5ca8rt, 4) == 0));
        gm_l2 = t4;
    }
    {
        {
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
            void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 880) + 0));
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
            gm_l3 = t10;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t12 = gm_l4;
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    gm_tf1bjg81u0glv t17;
                    uint64_t t18 = gm_l4;
                    GM_ST(uint64_t, (uint8_t *)&t17 + 0, t18);
                    uint64_t t19 = gm_l4;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t20 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t17 + 8, t22);
                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&t17) + 0));
                    gm_l5 = t25;
                    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
                    gm_l6 = t26;
                    {
                        uint64_t t28 = gm_l5;
                        uint64_t t29 = (uintptr_t)t28;
                        void * t30 = gm_l0;
                        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
                        void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
                        uint64_t t33 = GM_LD(uintptr_t, ((((uint8_t *)t32) + 920) + 0) + 8);
                        if (t29 >= (uintptr_t)(t33)) gm_trap_index(t29, t33, gm_srcd5arorrf8jkg, 613, 14);
                        uint32_t t34 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t32) + 920) + 0)) + t29 * 4));
                        gm_l7 = t34;
                    }
                    {
                        void * t35 = gm_l0;
                        void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
                        uint32_t t37 = gm_l7;
                        void * t38 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t36, t37);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        gm_l8 = t38;
                    }
                    {
                        uint64_t t39 = gm_l5;
                        gm_t4ce0jd3tdro4 t40;
                        GM_ST(uint64_t, ((uint8_t *)&t40 + 0), t39);
                        gm_tabb710d9mq2e t41;
                        gm_f_format_fpa95lg((void *)&t41, (void *)NULL, cx, NULL, t40);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 8, gm_srcd5arorrf8jkg, 617, 18);
                        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&t41) + 8));
                        gm_l9 = t42;
                    }
                    {
                        gm_f_list_epvducg((void *)&F.l10, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                    }
                    {
                        gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                    }
                    {
                        gm_f_list_uvfu1k1((void *)&F.l12, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                    }
                    {
                        {
                            void * t44 = gm_l8;
                            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)t44) + 32));
                            gm_l13 = t45;
                        }
                        {
                            gm_l14 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t47 = gm_l14;
                                uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                uint8_t t49 = (t47 < t48);
                                if (!t49) goto lb46;
                                {
                                    uint64_t t52 = gm_l14;
                                    uint64_t t53 = gm_l14;
                                    uint64_t t54 = (uintptr_t)t53;
                                    uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                    uint32_t t56 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t54 * 4));
                                    gm_tn68el8d14vku t57;
                                    GM_ST(uint32_t, (uint8_t *)&t57 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t57 + 0), t52);
                                    GM_ST(uint32_t, ((uint8_t *)&t57 + 8), t56);
                                    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&t57) + 0));
                                    gm_l15 = t60;
                                    uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)&t57) + 8));
                                    gm_l16 = t61;
                                    {
                                        uint32_t t63 = gm_l16;
                                        uint64_t t64 = (uint64_t)t63;
                                        uint64_t t65 = (uintptr_t)t64;
                                        void * t66 = gm_l8;
                                        uint64_t t67 = GM_LD(uintptr_t, (((uint8_t *)t66) + 48) + 8);
                                        if (t65 >= (uintptr_t)(t67)) gm_trap_index(t65, t67, gm_srcd5arorrf8jkg, 622, 18);
                                        gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t66) + 48)) + t65 * 40) + 16));
                                        gm_l17 = t68;
                                    }
                                    {
                                        void * t69 = gm_l0;
                                        gm_t88rcucg1b7uk t70 = gm_l17;
                                        void * t71 = gm_l8;
                                        gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t71) + 16));
                                        gm_t5o0tpv5jka2l t73;
                                        gm_f_c_abi_type_nu2c2tm((void *)&t73, (void *)NULL, cx, NULL, t69, t70, t72);
                                        if (gm_unw.kind) {
                                            goto gm_ll50;
                                        }
                                        if (((((uint8_t *)&t73))[0] != 0)) {
                                            gm_t5o0tpv5jka2l t74;
                                            GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                                            ((uint8_t *)&t74)[0] = 1;
                                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t74 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t73) + 8)));
                                            F.ret = t74;
                                            goto gm_fret;
                                        }
                                        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
                                        gm_l18 = t75;
                                    }
                                    {
                                        void * t76 = (void *)((uint8_t *)&F.l10);
                                        gm_t3877pk7lv748 t77;
                                        gm_b16_8 t78 = gm_l18;
                                        GM_ST(gm_b16_8, (uint8_t *)&t77 + 0, t78);
                                        uint64_t t79 = gm_l15;
                                        GM_ST(uint64_t, (uint8_t *)&t77 + 16, t79);
                                        gm_tabb710d9mq2e t80;
                                        gm_f_format_eq87866((void *)&t80, (void *)NULL, cx, NULL, &t77);
                                        if (gm_unw.kind) {
                                            goto gm_ll50;
                                        }
                                        if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 8, gm_srcd5arorrf8jkg, 624, 25);
                                        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&t80) + 8));
                                        gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                                        gm_tftb1odc739t5 t83 = gm_f_push_3ef746b(t76, t82);
                                        if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_srcd5arorrf8jkg, 624, 13);
                                        gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
                                    }
                                    {
                                        uint64_t t85 = gm_l15;
                                        gm_t4ce0jd3tdro4 t86;
                                        GM_ST(uint64_t, ((uint8_t *)&t86 + 0), t85);
                                        gm_tabb710d9mq2e t87;
                                        gm_f_format_l05am3t((void *)&t87, (void *)NULL, cx, NULL, t86);
                                        if (gm_unw.kind) {
                                            goto gm_ll50;
                                        }
                                        if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 8, gm_srcd5arorrf8jkg, 625, 18);
                                        gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&t87) + 8));
                                        gm_l19 = t88;
                                    }
                                    {
                                        uint8_t t90 = gm_l2;
                                        if (!t90) goto ie89;
                                        {
                                            uint64_t t92 = gm_l5;
                                            uint64_t t93 = gm_l15;
                                            gm_t2lqu8jsj511d t94;
                                            GM_ST(uint64_t, ((uint8_t *)&t94 + 0), t92);
                                            GM_ST(uint64_t, ((uint8_t *)&t94 + 8), t93);
                                            gm_tabb710d9mq2e t95;
                                            gm_f_format_6vickkk((void *)&t95, (void *)NULL, cx, NULL, t94);
                                            if (gm_unw.kind) {
                                                goto gm_ll50;
                                            }
                                            if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 8, gm_srcd5arorrf8jkg, 627, 21);
                                            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&t95) + 8));
                                            gm_l19 = t96;
                                        }
                                        {
                                            void * t97 = (void *)((uint8_t *)&F.l1);
                                            gm_t6j6smj7qoctu t98;
                                            gm_b16_8 t99 = gm_l18;
                                            GM_ST(gm_b16_8, (uint8_t *)&t98 + 0, t99);
                                            gm_b16_8 t100 = gm_l19;
                                            GM_ST(gm_b16_8, (uint8_t *)&t98 + 16, t100);
                                            gm_tftb1odc739t5 t101 = gm_f_write_2uh64vr(t97, &t98);
                                            if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 1, gm_srcd5arorrf8jkg, 628, 17);
                                            gm_b0_1 t102 = GM_LD(gm_b0_1, (((uint8_t *)&t101) + 1));
                                        }
                                        {
                                            void * t103 = (void *)((uint8_t *)&F.l12);
                                            gm_t3877pk7lv748 t104;
                                            gm_b16_8 t105 = gm_l19;
                                            GM_ST(gm_b16_8, (uint8_t *)&t104 + 0, t105);
                                            uint64_t t106 = gm_l15;
                                            GM_ST(uint64_t, (uint8_t *)&t104 + 16, t106);
                                            gm_tftb1odc739t5 t107 = gm_f_write_4uefpre(t103, &t104);
                                            if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_srcd5arorrf8jkg, 629, 17);
                                            gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
                                        }
                                        goto ix89;
ie89:;
ix89:;
                                    }
                                    {
                                        void * t109 = (void *)((uint8_t *)&F.l11);
                                        gm_b16_8 t110;
                                        void * t112 = gm_l0;
                                        void * t113 = (void *)(((uint8_t *)t112) + 8);
                                        gm_t88rcucg1b7uk t114 = gm_l17;
                                        gm_th2lqbo77pvsc t115;
                                        gm_f_mirror_lcjegsf((void *)&t115, (void *)NULL, cx, NULL, t113, t114);
                                        if (gm_unw.kind) {
                                            goto gm_ll50;
                                        }
                                        uint8_t t116;
                                        if (((((uint8_t *)&t115))[16] != 0)) goto mn118;
                                        t116 = ((uint8_t)0);
                                        goto me117;
mn118:;
                                        t116 = ((uint8_t)1);
                                        goto me117;
mn119:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 632, 26);
me117:;
                                        if (!t116) goto ie111;
                                        gm_b16_8 t121 = gm_l19;
                                        gm_b16_8 t122 = t121;
                                        gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
                                        t110 = t123;
                                        goto ix111;
ie111:;
                                        gm_t6j6smj7qoctu t125;
                                        void * t126 = gm_l0;
                                        void * t127 = (void *)(((uint8_t *)t126) + 8);
                                        gm_t88rcucg1b7uk t128 = gm_l17;
                                        gm_b16_8 t129 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t127, t128);
                                        if (gm_unw.kind) {
                                            goto gm_ll50;
                                        }
                                        GM_ST(gm_b16_8, (uint8_t *)&t125 + 0, t129);
                                        gm_b16_8 t130 = gm_l19;
                                        GM_ST(gm_b16_8, (uint8_t *)&t125 + 16, t130);
                                        gm_tabb710d9mq2e t131;
                                        gm_f_format_or32na5((void *)&t131, (void *)NULL, cx, NULL, &t125);
                                        if (gm_unw.kind) {
                                            goto gm_ll50;
                                        }
                                        if (((((uint8_t *)&t131))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t131) + 8, gm_srcd5arorrf8jkg, 632, 66);
                                        gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&t131) + 8));
                                        gm_b16_8 t133 = t132;
                                        gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                                        t110 = t134;
ix111:;
                                        gm_tftb1odc739t5 t135 = gm_f_push_3ef746b(t109, t110);
                                        if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_srcd5arorrf8jkg, 632, 13);
                                        gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
                                    }
                                    goto me58;
mn59:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 621, 9);
me58:;
                                }
lc46:;
                                {
                                    uint64_t t138 = gm_l14;
                                    uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                                    uint64_t t141 = r139;
                                    gm_l14 = t141;
                                }
                            }
gm_ll50:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb46; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc46; }
                            }
                            goto gm_ll15;
lb46:;
                        }
                    }
                    {
                        gm_t6j6smj7qoctu t142;
                        void * t143 = gm_l0;
                        uint32_t t144 = gm_l7;
                        gm_b16_8 t145 = gm_f_addr_name_g5lrlt3((void *)NULL, cx, NULL, t143, t144);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t142 + 0, t145);
                        gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                        gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
                        gm_b16_8 t148;
                        GM_ST(void *, (uint8_t *)&t148, (void *)(gm_sg5gdnrith2i6));
                        GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(2));
                        gm_tabb710d9mq2e t149;
                        gm_f_join_sfknljb((void *)&t149, (void *)NULL, cx, NULL, t147, t148);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        if (((((uint8_t *)&t149))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t149) + 8, gm_srcd5arorrf8jkg, 634, 54);
                        gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)&t149) + 8));
                        GM_ST(gm_b16_8, (uint8_t *)&t142 + 16, t150);
                        gm_tabb710d9mq2e t151;
                        gm_f_format_99gc605((void *)&t151, (void *)NULL, cx, NULL, &t142);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 8, gm_srcd5arorrf8jkg, 634, 17);
                        gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&t151) + 8));
                        gm_l20 = t152;
                    }
                    {
                        gm_b16_8 t153;
                        uint64_t t155 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                        uint8_t t156 = ((t155 == ((uint64_t)0ULL)));
                        if (!t156) goto ie154;
                        gm_b16_8 t158;
                        GM_ST(void *, (uint8_t *)&t158, (void *)(gm_sts5j6f5pkov0));
                        GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(4));
                        gm_b16_8 t159 = t158;
                        t153 = t159;
                        goto ix154;
ie154:;
                        gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                        gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                        gm_b16_8 t163;
                        GM_ST(void *, (uint8_t *)&t163, (void *)(gm_sg5gdnrith2i6));
                        GM_ST(uintptr_t, (uint8_t *)&t163 + 8, (uintptr_t)(2));
                        gm_tabb710d9mq2e t164;
                        gm_f_join_sfknljb((void *)&t164, (void *)NULL, cx, NULL, t162, t163);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        if (((((uint8_t *)&t164))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t164) + 8, gm_srcd5arorrf8jkg, 635, 73);
                        gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&t164) + 8));
                        gm_b16_8 t166 = t165;
                        gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
                        t153 = t167;
ix154:;
                        gm_l21 = t153;
                    }
                    {
                        gm_b16_8 t168;
                        void * t170 = gm_l0;
                        void * t171 = GM_LD(void *, (((uint8_t *)t170) + 0));
                        gm_b16_8 t172 = GM_LD(gm_b16_8, ((((uint8_t *)t171) + 80) + 24));
                        gm_t6j6smj7qoctu t173;
                        gm_b16_8 t174;
                        GM_ST(void *, (uint8_t *)&t174, (void *)(gm_soo4rtckloi4d));
                        GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(12));
                        GM_ST(gm_b16_8, (uint8_t *)&t173 + 0, t174);
                        gm_b16_8 t175;
                        GM_ST(void *, (uint8_t *)&t175, (void *)(gm_svl13be5ca8rt));
                        GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(4));
                        GM_ST(gm_b16_8, (uint8_t *)&t173 + 16, t175);
                        uint8_t t176 = 0;
                        if (gm_eq0iqiubfm7e(((uint8_t *)&t173) + 0, ((uint8_t *)&t172))) t176 = 1;
                        if (gm_eq0iqiubfm7e(((uint8_t *)&t173) + 16, ((uint8_t *)&t172))) t176 = 1;
                        uint8_t t177 = (uint8_t)(t176);
                        if (!t177) goto ie169;
                        gm_b16_8 t179;
                        GM_ST(void *, (uint8_t *)&t179, (void *)(gm_sobumja6poqii));
                        GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(12));
                        gm_b16_8 t180 = t179;
                        t168 = t180;
                        goto ix169;
ie169:;
                        gm_b16_8 t182;
                        GM_ST(void *, (uint8_t *)&t182, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(0));
                        gm_b16_8 t183 = t182;
                        t168 = t183;
ix169:;
                        gm_l22 = t168;
                    }
                    {
                        gm_b16_8 t184;
                        GM_ST(void *, (uint8_t *)&t184, (void *)(gm_sts5j6f5pkov0));
                        GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(4));
                        gm_l23 = t184;
                    }
                    {
                        gm_b16_8 t185;
                        GM_ST(void *, (uint8_t *)&t185, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)(0));
                        gm_l24 = t185;
                    }
                    {
                        void * t187 = gm_l0;
                        void * t188 = (void *)(((uint8_t *)t187) + 8);
                        void * t189 = gm_l8;
                        gm_t88rcucg1b7uk t190 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t189) + 64));
                        uint8_t t191 = gm_f_is_unit_17uennd(t188, GM_CONV(gm_csguankko4lmhd, t190));
                        uint8_t t192 = (uint8_t)!t191;
                        if (!t192) goto ie186;
                        {
                            void * t194 = gm_l0;
                            void * t195 = gm_l8;
                            gm_t88rcucg1b7uk t196 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t195) + 64));
                            void * t197 = gm_l8;
                            gm_tl874t2atnlh3 t198 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t197) + 16));
                            gm_t5o0tpv5jka2l t199;
                            gm_f_c_abi_type_nu2c2tm((void *)&t199, (void *)NULL, cx, NULL, t194, t196, t198);
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                            if (((((uint8_t *)&t199))[0] != 0)) {
                                gm_t5o0tpv5jka2l t200;
                                GM_ST(uint8_t, (uint8_t *)&t200 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t200 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t200 + 4, 0);
                                ((uint8_t *)&t200)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t200 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t199) + 8)));
                                F.ret = t200;
                                goto gm_fret;
                            }
                            gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)&t199) + 8));
                            gm_l23 = t201;
                        }
                        {
                            gm_b16_8 t202;
                            void * t204 = gm_l0;
                            void * t205 = (void *)(((uint8_t *)t204) + 8);
                            void * t206 = gm_l8;
                            gm_t88rcucg1b7uk t207 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t206) + 64));
                            gm_th2lqbo77pvsc t208;
                            gm_f_mirror_lcjegsf((void *)&t208, (void *)NULL, cx, NULL, t205, t207);
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                            uint8_t t209;
                            if (((((uint8_t *)&t208))[16] != 0)) goto mn211;
                            t209 = ((uint8_t)0);
                            goto me210;
mn211:;
                            t209 = ((uint8_t)1);
                            goto me210;
mn212:;
                            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 643, 24);
me210:;
                            if (!t209) goto ie203;
                            gm_b16_8 t214 = gm_l20;
                            gm_b16_8 t215 = t214;
                            gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
                            t202 = t216;
                            goto ix203;
ie203:;
                            gm_t6j6smj7qoctu t218;
                            gm_b16_8 t219 = gm_l23;
                            GM_ST(gm_b16_8, (uint8_t *)&t218 + 0, t219);
                            gm_b16_8 t220 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t218 + 16, t220);
                            gm_tabb710d9mq2e t221;
                            gm_f_format_or32na5((void *)&t221, (void *)NULL, cx, NULL, &t218);
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                            if (((((uint8_t *)&t221))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t221) + 8, gm_srcd5arorrf8jkg, 643, 73);
                            gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)&t221) + 8));
                            gm_b16_8 t223 = t222;
                            gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)&t223));
                            t202 = t224;
ix203:;
                            gm_l24 = t202;
                        }
                        goto ix186;
ie186:;
ix186:;
                    }
                    {
                        gm_b16_8 t225;
                        uint64_t t227 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                        uint8_t t228 = ((t227 == ((uint64_t)0ULL)));
                        if (!t228) goto ie226;
                        gm_t6j6smj7qoctu t230;
                        gm_b16_8 t231 = gm_l22;
                        GM_ST(gm_b16_8, (uint8_t *)&t230 + 0, t231);
                        gm_b16_8 t232 = gm_l20;
                        GM_ST(gm_b16_8, (uint8_t *)&t230 + 16, t232);
                        gm_tabb710d9mq2e t233;
                        gm_f_format_vmlgaaq((void *)&t233, (void *)NULL, cx, NULL, &t230);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        if (((((uint8_t *)&t233))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t233) + 8, gm_srcd5arorrf8jkg, 645, 44);
                        gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)&t233) + 8));
                        gm_b16_8 t235 = t234;
                        gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
                        t225 = t236;
                        goto ix226;
ie226:;
                        gm_t6j6smj7qoctu t238;
                        gm_b16_8 t239 = gm_l22;
                        GM_ST(gm_b16_8, (uint8_t *)&t238 + 0, t239);
                        gm_b16_8 t240 = gm_l24;
                        GM_ST(gm_b16_8, (uint8_t *)&t238 + 16, t240);
                        gm_tabb710d9mq2e t241;
                        gm_f_format_vp0clr1((void *)&t241, (void *)NULL, cx, NULL, &t238);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        if (((((uint8_t *)&t241))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t241) + 8, gm_srcd5arorrf8jkg, 645, 87);
                        gm_b16_8 t242 = GM_LD(gm_b16_8, (((uint8_t *)&t241) + 8));
                        gm_b16_8 t243 = t242;
                        gm_b16_8 t244 = GM_LD(gm_b16_8, ((uint8_t *)&t243));
                        t225 = t244;
ix226:;
                        gm_l25 = t225;
                    }
                    {
                        uint8_t t246 = gm_l2;
                        if (!t246) goto ie245;
                        {
                            uint64_t t248 = gm_l5;
                            gm_t4ce0jd3tdro4 t249;
                            GM_ST(uint64_t, ((uint8_t *)&t249 + 0), t248);
                            gm_tabb710d9mq2e t250;
                            gm_f_format_lrgko01((void *)&t250, (void *)NULL, cx, NULL, t249);
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                            if (((((uint8_t *)&t250))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t250) + 8, gm_srcd5arorrf8jkg, 647, 23);
                            gm_b16_8 t251 = GM_LD(gm_b16_8, (((uint8_t *)&t250) + 8));
                            gm_l26 = t251;
                        }
                        {
                            uint64_t t253 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                            uint8_t t254 = ((t253 == ((uint64_t)0ULL)));
                            if (!t254) goto ie252;
                            {
                                void * t256 = (void *)((uint8_t *)&F.l1);
                                gm_tf1bjg81u0glv t257;
                                uint64_t t258 = gm_l5;
                                GM_ST(uint64_t, (uint8_t *)&t257 + 0, t258);
                                gm_b16_8 t259 = gm_l20;
                                GM_ST(gm_b16_8, (uint8_t *)&t257 + 8, t259);
                                gm_tftb1odc739t5 t260 = gm_f_write_d85j2rp(t256, &t257);
                                if (((((uint8_t *)&t260))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t260) + 1, gm_srcd5arorrf8jkg, 649, 17);
                                gm_b0_1 t261 = GM_LD(gm_b0_1, (((uint8_t *)&t260) + 1));
                            }
                            {
                                gm_tftksr4op114e t262;
                                gm_b16_8 t263 = gm_l22;
                                GM_ST(gm_b16_8, (uint8_t *)&t262 + 0, t263);
                                gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t262 + 16, t264);
                                uint64_t t265 = gm_l5;
                                GM_ST(uint64_t, (uint8_t *)&t262 + 32, t265);
                                gm_tabb710d9mq2e t266;
                                gm_f_format_avhd7c9((void *)&t266, (void *)NULL, cx, NULL, &t262);
                                if (gm_unw.kind) {
                                    goto gm_ll15;
                                }
                                if (((((uint8_t *)&t266))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t266) + 8, gm_srcd5arorrf8jkg, 650, 24);
                                gm_b16_8 t267 = GM_LD(gm_b16_8, (((uint8_t *)&t266) + 8));
                                gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&t267));
                                gm_l25 = t268;
                            }
                            goto ix252;
ie252:;
                            {
                                void * t270 = (void *)((uint8_t *)&F.l1);
                                gm_ti71ll5knnvij t271;
                                gm_b16_8 t272 = gm_l23;
                                GM_ST(gm_b16_8, (uint8_t *)&t271 + 0, t272);
                                gm_b16_8 t273 = gm_l26;
                                GM_ST(gm_b16_8, (uint8_t *)&t271 + 16, t273);
                                uint64_t t274 = gm_l5;
                                GM_ST(uint64_t, (uint8_t *)&t271 + 32, t274);
                                gm_b16_8 t275 = gm_l26;
                                GM_ST(gm_b16_8, (uint8_t *)&t271 + 40, t275);
                                gm_b16_8 t276 = gm_l24;
                                GM_ST(gm_b16_8, (uint8_t *)&t271 + 56, t276);
                                gm_tftb1odc739t5 t277 = gm_f_write_gk1hd5k(t270, &t271);
                                if (((((uint8_t *)&t277))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t277) + 1, gm_srcd5arorrf8jkg, 652, 17);
                                gm_b0_1 t278 = GM_LD(gm_b0_1, (((uint8_t *)&t277) + 1));
                            }
                            {
                                gm_tmr5oep1578tb t279;
                                gm_b16_8 t280 = gm_l22;
                                GM_ST(gm_b16_8, (uint8_t *)&t279 + 0, t280);
                                gm_b16_8 t281 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t279 + 16, t281);
                                uint64_t t282 = gm_l5;
                                GM_ST(uint64_t, (uint8_t *)&t279 + 32, t282);
                                gm_b16_8 t283 = gm_l26;
                                GM_ST(gm_b16_8, (uint8_t *)&t279 + 40, t283);
                                gm_tabb710d9mq2e t284;
                                gm_f_format_8v2pi8t((void *)&t284, (void *)NULL, cx, NULL, &t279);
                                if (gm_unw.kind) {
                                    goto gm_ll15;
                                }
                                if (((((uint8_t *)&t284))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t284) + 8, gm_srcd5arorrf8jkg, 653, 24);
                                gm_b16_8 t285 = GM_LD(gm_b16_8, (((uint8_t *)&t284) + 8));
                                gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&t285));
                                gm_l25 = t286;
                            }
ix252:;
                        }
                        goto ix245;
ie245:;
ix245:;
                    }
                    {
                        void * t287 = gm_l8;
                        uint8_t t288 = GM_LD(uint8_t, (((uint8_t *)t287) + 144));
                        gm_b16_8 t289 = gm_f_conv_attr_m0b89pc(t288);
                        gm_l27 = t289;
                    }
                    {
                        uint8_t t291 = gm_l2;
                        if (!t291) goto ie290;
                        {
                            gm_t6j6smj7qoctu t293;
                            gm_b16_8 t294 = gm_l6;
                            GM_ST(gm_b16_8, (uint8_t *)&t293 + 0, t294);
                            gm_b16_8 t295 = gm_l27;
                            GM_ST(gm_b16_8, (uint8_t *)&t293 + 16, t295);
                            gm_tabb710d9mq2e t296;
                            gm_f_format_eg7p593((void *)&t296, (void *)NULL, cx, NULL, &t293);
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                            if (((((uint8_t *)&t296))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t296) + 8, gm_srcd5arorrf8jkg, 658, 24);
                            gm_b16_8 t297 = GM_LD(gm_b16_8, (((uint8_t *)&t296) + 8));
                            gm_b16_8 t298 = GM_LD(gm_b16_8, ((uint8_t *)&t297));
                            gm_l27 = t298;
                        }
                        goto ix290;
ie290:;
ix290:;
                    }
                    {
                        void * t299 = (void *)((uint8_t *)&F.l1);
                        gm_tud7oovfvnf43 t300;
                        gm_b16_8 t301 = gm_l27;
                        GM_ST(gm_b16_8, (uint8_t *)&t300 + 0, t301);
                        gm_b16_8 t302 = gm_l23;
                        GM_ST(gm_b16_8, (uint8_t *)&t300 + 16, t302);
                        gm_b16_8 t303 = gm_l9;
                        GM_ST(gm_b16_8, (uint8_t *)&t300 + 32, t303);
                        gm_b16_8 t304 = gm_l21;
                        GM_ST(gm_b16_8, (uint8_t *)&t300 + 48, t304);
                        gm_b16_8 t305 = gm_l6;
                        GM_ST(gm_b16_8, (uint8_t *)&t300 + 64, t305);
                        gm_tftb1odc739t5 t306 = gm_f_write_okup0j2(t299, &t300);
                        if (((((uint8_t *)&t306))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t306) + 1, gm_srcd5arorrf8jkg, 659, 9);
                        gm_b0_1 t307 = GM_LD(gm_b0_1, (((uint8_t *)&t306) + 1));
                    }
                    {
                        void * t308 = (void *)((uint8_t *)&F.l1);
                        gm_tud7oovfvnf43 t309;
                        gm_b16_8 t310 = gm_l27;
                        GM_ST(gm_b16_8, (uint8_t *)&t309 + 0, t310);
                        gm_b16_8 t311 = gm_l23;
                        GM_ST(gm_b16_8, (uint8_t *)&t309 + 16, t311);
                        gm_b16_8 t312 = gm_l9;
                        GM_ST(gm_b16_8, (uint8_t *)&t309 + 32, t312);
                        gm_b16_8 t313 = gm_l21;
                        GM_ST(gm_b16_8, (uint8_t *)&t309 + 48, t313);
                        gm_b16_8 t314 = gm_l25;
                        GM_ST(gm_b16_8, (uint8_t *)&t309 + 64, t314);
                        gm_tftb1odc739t5 t315 = gm_f_write_ticpdcl(t308, &t309);
                        if (((((uint8_t *)&t315))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t315) + 1, gm_srcd5arorrf8jkg, 660, 9);
                        gm_b0_1 t316 = GM_LD(gm_b0_1, (((uint8_t *)&t315) + 1));
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 612, 5);
me23:;
                }
lc11:;
                {
                    uint64_t t318 = gm_l4;
                    uint64_t r319; bool o320 = __builtin_add_overflow(t318, ((uint64_t)1ULL), &r319);
                    uint64_t t321 = r319;
                    gm_l4 = t321;
                }
            }
gm_ll15:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
            }
            goto gm_lad;
lb11:;
        }
    }
    {
        gm_b16_8 t322 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t323 = GM_LD(gm_b16_8, ((uint8_t *)&t322));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t323);
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
__attribute__((aligned(4))) void gm_f_gen_main_0gsgsok(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_gen_main_0gsgsok F;
    void * gm_l0; /* g */
    gm_b16_8 gm_l1; /* entries */
    uint8_t gm_l2; /* select */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint32_t gm_l8; /* entry */
    gm_tk8ebsa8coi4p gm_l9; /* a */
    gm_tv199o0mcsqd6 gm_l10; /* amb */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    gm_t9805qtonbsag gm_l14; /* e */
    uint32_t gm_l15; /* eff */
    gm_tiilmfd16v3fc gm_l16; /* r */
    uint32_t gm_l17; /* id */
    GM_STACK_CHECK(&F, 1296u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_l5 = t3;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l6;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l6;
                    uint64_t t11 = gm_l6;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint32_t t14 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t12 * 4));
                    gm_tn68el8d14vku t15;
                    GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t10);
                    GM_ST(uint32_t, ((uint8_t *)&t15 + 8), t14);
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
                    gm_l7 = t18;
                    uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&t15) + 8));
                    gm_l8 = t19;
                    {
                        void * t21 = (void *)((uint8_t *)&F.l3);
                        void * t22 = gm_l0;
                        uint32_t t23 = gm_l8;
                        uint64_t t24 = gm_l7;
                        gm_t5o0tpv5jka2l t25;
                        gm_f_gen_entry_hj4odev((void *)&t25, (void *)NULL, cx, NULL, t22, t23, t24);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        if (((((uint8_t *)&t25))[0] != 0)) {
                            gm_t5o0tpv5jka2l t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                            ((uint8_t *)&t26)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t26 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t25) + 8)));
                            F.ret = t26;
                            goto gm_fret;
                        }
                        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
                        gm_tftb1odc739t5 t28 = gm_f_extend_0cs85gs(t21, t27);
                        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srcd5arorrf8jkg, 675, 9);
                        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
                    }
                    {
                        void * t30 = (void *)((uint8_t *)&F.l4);
                        uint64_t t31 = gm_l7;
                        uint64_t t32 = gm_l7;
                        gm_t2lqu8jsj511d t33;
                        GM_ST(uint64_t, ((uint8_t *)&t33 + 0), t31);
                        GM_ST(uint64_t, ((uint8_t *)&t33 + 8), t32);
                        gm_tftb1odc739t5 t34 = gm_f_write_lu60214(t30, t33);
                        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srcd5arorrf8jkg, 676, 9);
                        gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 674, 5);
me16:;
                }
lc4:;
                {
                    uint64_t t37 = gm_l6;
                    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                    uint64_t t40 = r38;
                    gm_l6 = t40;
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
        void * t41 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_seosa3iobfc09));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(34));
        gm_tftb1odc739t5 t43 = gm_f_extend_0cs85gs(t41, t42);
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcd5arorrf8jkg, 678, 5);
        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
    }
    {
        uint8_t t46 = gm_l2;
        if (!t46) goto ie45;
        {
            void * t48 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sv7tbc2gvmpa1));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(99));
            gm_tftb1odc739t5 t50 = gm_f_extend_0cs85gs(t48, t49);
            if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_srcd5arorrf8jkg, 680, 9);
            gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
        }
        goto ix45;
ie45:;
        {
            void * t53 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t54;
            GM_ST(void *, (uint8_t *)&t54, (void *)(gm_slbs27elv52b5));
            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(29));
            gm_tftb1odc739t5 t55 = gm_f_extend_0cs85gs(t53, t54);
            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_srcd5arorrf8jkg, 682, 9);
            gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
        }
ix45:;
    }
    {
        void * t57 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t58;
        GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sq77ose3aa457));
        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(23));
        gm_tftb1odc739t5 t59 = gm_f_extend_0cs85gs(t57, t58);
        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srcd5arorrf8jkg, 684, 5);
        gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
    }
    {
        void * t62 = gm_l0;
        void * t63 = GM_LD(void *, (((uint8_t *)t62) + 0));
        uint8_t t64 = (((GM_LD(uintptr_t, ((((uint8_t *)t63) + 80) + 24) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)t63) + 80) + 24)), gm_svl13be5ca8rt, 4) == 0));
        if (!t64) goto ie61;
        void * t65 = gm_l0;
        void * t66 = GM_LD(void *, (((uint8_t *)t65) + 0));
        void * t67 = GM_LD(void *, (((uint8_t *)t66) + 0));
        uint64_t t68 = GM_LD(uintptr_t, ((((uint8_t *)t67) + 880) + 0) + 8);
        uint8_t t69 = (t68 > ((uint64_t)0ULL));
        if (!t69) goto ie61;
        {
            void * t71 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t72;
            GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s2nn3moc8tfri));
            GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(16));
            gm_tftb1odc739t5 t73 = gm_f_extend_0cs85gs(t71, t72);
            if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_srcd5arorrf8jkg, 687, 9);
            gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
        }
        goto ix61;
ie61:;
        {
            void * t76 = (void *)((uint8_t *)&F.l3);
            int32_t t77;
            void * t79 = gm_l0;
            uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)t79) + 1136) + 1));
            if (!t80) goto ie78;
            int32_t t82 = ((int32_t)1LL);
            t77 = t82;
            goto ix78;
ie78:;
            int32_t t84 = ((int32_t)0LL);
            t77 = t84;
ix78:;
            gm_t19enj90vg8bd t85;
            GM_ST(int32_t, ((uint8_t *)&t85 + 0), t77);
            gm_tftb1odc739t5 t86 = gm_f_write_gi12t4o(t76, t85);
            if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srcd5arorrf8jkg, 689, 9);
            gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
        }
        {
            void * t88 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t89;
            GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sck2rq9li012q));
            GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(22));
            gm_tftb1odc739t5 t90 = gm_f_extend_0cs85gs(t88, t89);
            if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 1, gm_srcd5arorrf8jkg, 690, 9);
            gm_b0_1 t91 = GM_LD(gm_b0_1, (((uint8_t *)&t90) + 1));
        }
ix61:;
    }
    {
        void * t92 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t93;
        GM_ST(void *, (uint8_t *)&t93, (void *)(gm_smdv045psk1u8));
        GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(24));
        gm_tftb1odc739t5 t94 = gm_f_extend_0cs85gs(t92, t93);
        if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_srcd5arorrf8jkg, 692, 5);
        gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
    }
    {
        uint8_t t97 = gm_l2;
        if (!t97) goto ie96;
        void * t98 = gm_l0;
        void * t99 = GM_LD(void *, (((uint8_t *)t98) + 0));
        void * t100 = GM_LD(void *, (((uint8_t *)t99) + 0));
        uint32_t t101 = GM_LD(uint32_t, (((uint8_t *)t100) + 2160));
        uint8_t t102 = (t101 > ((uint32_t)0ULL));
        if (!t102) goto ie96;
        {
            void * t104 = (void *)((uint8_t *)&F.l3);
            void * t105 = gm_l0;
            void * t106 = GM_LD(void *, (((uint8_t *)t105) + 0));
            void * t107 = GM_LD(void *, (((uint8_t *)t106) + 0));
            uint32_t t108 = GM_LD(uint32_t, (((uint8_t *)t107) + 2160));
            gm_t88rcucg1b7uk t109;
            GM_ST(uint32_t, ((uint8_t *)&t109 + 0), t108);
            gm_tftb1odc739t5 t110 = gm_f_write_j5p5p7n(t104, t109);
            if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 1, gm_srcd5arorrf8jkg, 696, 9);
            gm_b0_1 t111 = GM_LD(gm_b0_1, (((uint8_t *)&t110) + 1));
        }
        goto ix96;
ie96:;
ix96:;
    }
    {
        void * t112 = gm_l0;
        void * t113 = GM_LD(void *, (((uint8_t *)t112) + 0));
        gm_tk8ebsa8coi4p t114 = GM_LD(gm_tk8ebsa8coi4p, (((uint8_t *)t113) + 48));
        gm_l9 = t114;
    }
    {
        gm_t9805qtonbsag t115 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l9) + 0));
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&gm_l10 + 0, t115);
        gm_t9805qtonbsag t116 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l9) + 8));
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&gm_l10 + 8, t116);
        gm_t9805qtonbsag t117 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l9) + 16));
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&gm_l10 + 16, t117);
    }
    {
        {
            void * t119 = ((uint8_t *)&gm_l10);
            if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcd5arorrf8jkg, 700, 27);
            gm_b16_8 t120;
            GM_ST(void *, (uint8_t *)&t120, (void *)(((uint8_t *)t119 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
            gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
            gm_l11 = t121;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t123 = gm_l12;
                uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t125 = (t123 < t124);
                if (!t125) goto lb122;
                {
                    uint64_t t128 = gm_l12;
                    uint64_t t129 = gm_l12;
                    uint64_t t130 = (uintptr_t)t129;
                    uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_t9805qtonbsag t132 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t130 * 8));
                    gm_tp0pp2cmr6tvd t133;
                    GM_ST(uint64_t, ((uint8_t *)&t133 + 0), t128);
                    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t133 + 8), t132);
                    uint64_t t136 = GM_LD(uint64_t, (((uint8_t *)&t133) + 0));
                    gm_l13 = t136;
                    gm_t9805qtonbsag t137 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&t133) + 8));
                    gm_l14 = t137;
                    {
                        gm_t9805qtonbsag t139 = gm_l14;
                        uint32_t t140;
                        if (((((uint8_t *)&t139))[4] != 0)) {
                            uint32_t t141 = GM_LD(uint32_t, (((uint8_t *)&t139) + 0));
                            t140 = t141;
                        } else {
                            goto lc122;
                        }
                        gm_l15 = t140;
                    }
                    {
                        memset((uint8_t *)&gm_l16 + 4, 0, 4);
                        uint32_t t142 = gm_l15;
                        GM_ST(uint32_t, (uint8_t *)&gm_l16 + 0, t142);
                        gm_b16_8 t143 = gm_f_empty_slice_fh7svmj();
                        gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                        GM_ST(gm_b16_8, (uint8_t *)&gm_l16 + 8, t144);
                    }
                    {
                        void * t145 = gm_l0;
                        void * t146 = (void *)((uint8_t *)&gm_l16);
                        uint32_t t147 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t145, t146);
                        if (gm_unw.kind) {
                            goto gm_ll126;
                        }
                        gm_l17 = t147;
                    }
                    {
                        void * t148 = (void *)((uint8_t *)&F.l3);
                        gm_tnen6863q2sjs t149;
                        memset((uint8_t *)&t149 + 20, 0, 4);
                        uint64_t t150 = gm_l13;
                        GM_ST(uint64_t, (uint8_t *)&t149 + 0, t150);
                        uint64_t t151 = gm_l13;
                        GM_ST(uint64_t, (uint8_t *)&t149 + 8, t151);
                        uint32_t t152 = gm_l17;
                        GM_ST(uint32_t, (uint8_t *)&t149 + 16, t152);
                        uint64_t t153 = gm_l13;
                        GM_ST(uint64_t, (uint8_t *)&t149 + 24, t153);
                        gm_tftb1odc739t5 t154 = gm_f_write_ug67q96(t148, &t149);
                        if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 1, gm_srcd5arorrf8jkg, 704, 9);
                        gm_b0_1 t155 = GM_LD(gm_b0_1, (((uint8_t *)&t154) + 1));
                    }
                    goto me134;
mn135:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 700, 5);
me134:;
                }
lc122:;
                {
                    uint64_t t157 = gm_l12;
                    uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)1ULL), &r158);
                    uint64_t t160 = r158;
                    gm_l12 = t160;
                }
            }
gm_ll126:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb122; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc122; }
            }
            goto gm_lad;
lb122:;
        }
    }
    {
        uint8_t t162 = gm_l2;
        if (!t162) goto ie161;
        {
            void * t164 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_tnpifv20irnph t166;
            GM_ST(gm_b16_8, ((uint8_t *)&t166 + 0), t165);
            gm_tftb1odc739t5 t167 = gm_f_write_jqf697l(t164, t166);
            if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 1, gm_srcd5arorrf8jkg, 707, 9);
            gm_b0_1 t168 = GM_LD(gm_b0_1, (((uint8_t *)&t167) + 1));
        }
        goto ix161;
ie161:;
        {
            void * t170 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t171;
            GM_ST(void *, (uint8_t *)&t171, (void *)(gm_svgsom6hbqtcn));
            GM_ST(uintptr_t, (uint8_t *)&t171 + 8, (uintptr_t)(39));
            gm_tftb1odc739t5 t172 = gm_f_extend_0cs85gs(t170, t171);
            if (((((uint8_t *)&t172))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t172) + 1, gm_srcd5arorrf8jkg, 709, 9);
            gm_b0_1 t173 = GM_LD(gm_b0_1, (((uint8_t *)&t172) + 1));
        }
ix161:;
    }
    {
        void * t174 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t175;
        GM_ST(void *, (uint8_t *)&t175, (void *)(gm_sikpqtja8pqpv));
        GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t176 = gm_f_extend_0cs85gs(t174, t175);
        if (((((uint8_t *)&t176))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t176) + 1, gm_srcd5arorrf8jkg, 711, 5);
        gm_b0_1 t177 = GM_LD(gm_b0_1, (((uint8_t *)&t176) + 1));
    }
    {
        gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t179);
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
inline __attribute__((aligned(4))) void gm_f_generate_6no5tn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2) {
    struct gm_fr_generate_6no5tn1 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* entry */
    gm_tftmsj5vtqvej gm_l2; /* opts */
    gm_tdvq40fmif5j0 gm_l3; /* entries */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    {
        uint32_t t2 = gm_l1;
        gm_tdvq40fmif5j0 t3;
        GM_ST(uint32_t, ((uint8_t *)&t3 + 0), t2);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = ((uint8_t *)&gm_l3);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcd5arorrf8jkg, 159, 32);
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(((uint8_t *)t5 + (uintptr_t)0 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_f_generate_entries_br1bd7f((void *)&(*(gm_tetdkgoreqmn1 *)gm_out), (void *)NULL, cx, NULL, t4, t7, ((uint8_t)0), &(*(gm_tftmsj5vtqvej *)((uint8_t *)&gm_l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) void gm_f_generate_entries_br1bd7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, const void * a3) {
    struct gm_fr_generate_entries_br1bd7f F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* entries */
    uint8_t gm_l2; /* select */
    gm_tftmsj5vtqvej gm_l3; /* opts */
    void * gm_l4; /* <tmp> */
    gm_tftmsj5vtqvej gm_l6; /* <tmp> */
    gm_b16_8 gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    uint32_t gm_l54; /* entry */
    gm_b16_8 gm_l55; /* <tmp> */
    uint64_t gm_l56; /* <tmp> */
    uint32_t gm_l57; /* f */
    uint32_t gm_l58; /* hook */
    gm_t6j6smj7qoctu gm_l59; /* consts */
    gm_b16_8 gm_l60; /* exports */
    uint8_t gm_l61; /* freestanding */
    gm_b16_8 gm_l62; /* main */
    gm_b16_8 gm_l63; /* checks */
    gm_b16_8 gm_l65; /* names */
    gm_b16_8 gm_l66; /* gdb */
    gm_b16_8 gm_l68; /* <tmp> */
    uint64_t gm_l69; /* <tmp> */
    uint32_t gm_l70; /* id */
    gm_b16_8 gm_l71; /* frames */
    gm_b16_8 gm_l72; /* protos */
    gm_closure gm_l76; /* code_has */
    uint8_t gm_l77; /* fibers */
    gm_b16_8 gm_l78; /* run_main */
    gm_b16_8 gm_l79; /* main_text */
    gm_b16_8 gm_l80; /* reset */
    gm_b16_8 gm_l81; /* split_support */
    gm_b16_8 gm_l82; /* split_types */
    gm_b16_8 gm_l83; /* split_frames */
    gm_b16_8 gm_l84; /* <tmp> */
    gm_b16_8 gm_l85; /* <tmp> */
    gm_b16_8 gm_l86; /* <tmp> */
    gm_b16_8 gm_l87; /* <tmp> */
    gm_b16_8 gm_l88; /* <tmp> */
    gm_b16_8 gm_l89; /* <tmp> */
    gm_b16_8 gm_l90; /* <tmp> */
    gm_b16_8 gm_l91; /* <tmp> */
    gm_b16_8 gm_l92; /* <tmp> */
    gm_b16_8 gm_l93; /* <tmp> */
    gm_b16_8 gm_l94; /* <tmp> */
    GM_STACK_CHECK(&F, 6972u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l0;
            gm_l4 = t3;
        }
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
            void * t6 = gm_l0;
            gm_f_ctypes_j6au0st((void *)&F.l5, (void *)NULL, cx, NULL, t5, &(*(gm_t0rfe1ou14h21 *)(((uint8_t *)t6) + 80)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tftmsj5vtqvej t7 = GM_LD(gm_tftmsj5vtqvej, ((uint8_t *)a3));
            gm_l6 = t7;
        }
        {
            gm_f_hash_map_rs5nlfs((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_p3t5tj8((void *)&F.l8, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_new_namer_5q88o9g((void *)&F.l9, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_0c2v515((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_0c2v515((void *)&F.l11, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_0c2v515((void *)&F.l12, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_g5h3sdu((void *)&F.l13, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_tl7apid((void *)&F.l14, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_tl7apid((void *)&F.l15, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_0c2v515((void *)&F.l16, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_tl7apid((void *)&F.l17, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_hme58st((void *)&F.l18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_p3t5tj8((void *)&F.l19, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_uvfu1k1((void *)&F.l20, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_uvfu1k1((void *)&F.l21, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_uvfu1k1((void *)&F.l22, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_uvfu1k1((void *)&F.l23, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_new_helpers_4q783cn((void *)&F.l24, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_go2qg0b((void *)&F.l25, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_s83jn2q((void *)&F.l26, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_s83jn2q((void *)&F.l27, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_76d05hh((void *)&F.l28, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_41r6dkn((void *)&F.l29, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_rc6kc0u((void *)&F.l30, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_a5k8eig((void *)&F.l31, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_fupfd5g((void *)&F.l32, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_t9h7o8p((void *)&F.l33, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_k5efes8((void *)&F.l34, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_t9h7o8p((void *)&F.l35, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_em2q005((void *)&F.l36, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_uvfu1k1((void *)&F.l37, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_epvducg((void *)&F.l38, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_epvducg((void *)&F.l39, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_epvducg((void *)&F.l40, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_938p55h((void *)&F.l41, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_0vc6l5i((void *)&F.l42, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_uvfu1k1((void *)&F.l43, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_epvducg((void *)&F.l44, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_70470jk((void *)&F.l45, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_70470jk((void *)&F.l46, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_go2qg0b((void *)&F.l47, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_1s9eugt((void *)&F.l48, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_1t0a5et((void *)&F.l49, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_1c18lnh((void *)&F.l50, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t8 = gm_l4;
        GM_ST(void *, (uint8_t *)&F.l51 + 0, t8);
        memcpy((uint8_t *)&F.l51 + 8, ((uint8_t *)&F.l5), 1128);
        memcpy((uint8_t *)&F.l51 + 1136, ((uint8_t *)&gm_l6), 24);
        memcpy((uint8_t *)&F.l51 + 1160, ((uint8_t *)&F.l7), 56);
        memcpy((uint8_t *)&F.l51 + 1216, ((uint8_t *)&F.l8), 56);
        memcpy((uint8_t *)&F.l51 + 1272, ((uint8_t *)&F.l9), 56);
        memcpy((uint8_t *)&F.l51 + 1328, ((uint8_t *)&F.l10), 40);
        memcpy((uint8_t *)&F.l51 + 1368, ((uint8_t *)&F.l11), 40);
        memcpy((uint8_t *)&F.l51 + 1408, ((uint8_t *)&F.l12), 40);
        memcpy((uint8_t *)&F.l51 + 1448, ((uint8_t *)&F.l13), 56);
        memcpy((uint8_t *)&F.l51 + 1504, ((uint8_t *)&F.l14), 56);
        memcpy((uint8_t *)&F.l51 + 1560, ((uint8_t *)&F.l15), 56);
        memcpy((uint8_t *)&F.l51 + 1616, ((uint8_t *)&F.l16), 40);
        memcpy((uint8_t *)&F.l51 + 1656, ((uint8_t *)&F.l17), 56);
        memcpy((uint8_t *)&F.l51 + 1712, ((uint8_t *)&F.l18), 40);
        memcpy((uint8_t *)&F.l51 + 1752, ((uint8_t *)&F.l19), 56);
        memcpy((uint8_t *)&F.l51 + 1808, ((uint8_t *)&F.l20), 40);
        memcpy((uint8_t *)&F.l51 + 1848, ((uint8_t *)&F.l21), 40);
        memcpy((uint8_t *)&F.l51 + 1888, ((uint8_t *)&F.l22), 40);
        memcpy((uint8_t *)&F.l51 + 1928, ((uint8_t *)&F.l23), 40);
        memcpy((uint8_t *)&F.l51 + 1968, ((uint8_t *)&F.l24), 304);
        memcpy((uint8_t *)&F.l51 + 2272, ((uint8_t *)&F.l25), 40);
        memcpy((uint8_t *)&F.l51 + 2312, ((uint8_t *)&F.l26), 40);
        memcpy((uint8_t *)&F.l51 + 2352, ((uint8_t *)&F.l27), 40);
        memcpy((uint8_t *)&F.l51 + 2392, ((uint8_t *)&F.l28), 56);
        memcpy((uint8_t *)&F.l51 + 2448, ((uint8_t *)&F.l29), 56);
        gm_tqetrgo0q24b5 t9;
        memset(&t9, 0, sizeof t9);
        GM_ST(gm_tqetrgo0q24b5, (uint8_t *)&F.l51 + 2504, t9);
        memcpy((uint8_t *)&F.l51 + 2552, ((uint8_t *)&F.l30), 40);
        memcpy((uint8_t *)&F.l51 + 2592, ((uint8_t *)&F.l31), 40);
        memcpy((uint8_t *)&F.l51 + 2632, ((uint8_t *)&F.l32), 56);
        memcpy((uint8_t *)&F.l51 + 2688, ((uint8_t *)&F.l33), 56);
        memcpy((uint8_t *)&F.l51 + 2744, ((uint8_t *)&F.l34), 40);
        memcpy((uint8_t *)&F.l51 + 2784, ((uint8_t *)&F.l35), 56);
        memcpy((uint8_t *)&F.l51 + 2840, ((uint8_t *)&F.l36), 56);
        gm_tuutj662vpgtg t10;
        memset(&t10, 0, sizeof t10);
        GM_ST(gm_tuutj662vpgtg, (uint8_t *)&F.l51 + 2896, t10);
        memcpy((uint8_t *)&F.l51 + 2960, ((uint8_t *)&F.l41), 40);
        memcpy((uint8_t *)&F.l51 + 3000, ((uint8_t *)&F.l42), 40);
        memcpy((uint8_t *)&F.l51 + 3040, ((uint8_t *)&F.l43), 40);
        memcpy((uint8_t *)&F.l51 + 3080, ((uint8_t *)&F.l37), 40);
        memcpy((uint8_t *)&F.l51 + 3120, ((uint8_t *)&F.l38), 40);
        memcpy((uint8_t *)&F.l51 + 3160, ((uint8_t *)&F.l39), 40);
        memcpy((uint8_t *)&F.l51 + 3200, ((uint8_t *)&F.l40), 40);
        memcpy((uint8_t *)&F.l51 + 3240, ((uint8_t *)&F.l44), 40);
        memcpy((uint8_t *)&F.l51 + 3280, ((uint8_t *)&F.l45), 56);
        memcpy((uint8_t *)&F.l51 + 3336, ((uint8_t *)&F.l46), 56);
        memcpy((uint8_t *)&F.l51 + 3392, ((uint8_t *)&F.l47), 40);
        memcpy((uint8_t *)&F.l51 + 3432, ((uint8_t *)&F.l48), 56);
        memcpy((uint8_t *)&F.l51 + 3488, ((uint8_t *)&F.l49), 40);
        memcpy((uint8_t *)&F.l51 + 3528, ((uint8_t *)&F.l50), 56);
        gm_b16_8 t11 = GM_LD(gm_b16_8, gm_cgsmfcq937e_2);
        GM_ST(gm_b16_8, (uint8_t *)&F.l51 + 3584, t11);
    }
    {
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a3) + 2));
        GM_ST(uint8_t, ((((uint8_t *)&F.l51) + 8) + 808), t12);
    }
    {
        {
            gm_b16_8 t14 = gm_l1;
            gm_l52 = t14;
        }
        {
            gm_l53 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t16 = gm_l53;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                uint8_t t18 = (t16 < t17);
                if (!t18) goto lb15;
                {
                    uint64_t t21 = gm_l53;
                    uint64_t t22 = (uintptr_t)t21;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                    uint32_t t24 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l52)) + t22 * 4));
                    uint32_t t27 = GM_LD(uint32_t, ((uint8_t *)&t24));
                    gm_l54 = t27;
                    {
                        void * t29 = (void *)((uint8_t *)&F.l51);
                        uint32_t t30 = gm_l54;
                        gm_b0_1 t31 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t29, t30);
                        if (gm_unw.kind) {
                            goto gm_ll19;
                        }
                    }
                    goto me25;
mn26:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 226, 5);
me25:;
                }
lc15:;
                {
                    uint64_t t33 = gm_l53;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l53 = t36;
                }
            }
gm_ll19:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb15; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc15; }
            }
            goto gm_lad;
lb15:;
        }
    }
    {
        {
            void * t38 = gm_l0;
            void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((((uint8_t *)t39) + 920) + 0));
            gm_l55 = t40;
        }
        {
            gm_l56 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t42 = gm_l56;
                uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                uint8_t t44 = (t42 < t43);
                if (!t44) goto lb41;
                {
                    uint64_t t47 = gm_l56;
                    uint64_t t48 = (uintptr_t)t47;
                    uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                    uint32_t t50 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l55)) + t48 * 4));
                    uint32_t t53 = GM_LD(uint32_t, ((uint8_t *)&t50));
                    gm_l57 = t53;
                    {
                        void * t55 = (void *)((uint8_t *)&F.l51);
                        uint32_t t56 = gm_l57;
                        gm_b0_1 t57 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t55, t56);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                    }
                    goto me51;
mn52:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 227, 5);
me51:;
                }
lc41:;
                {
                    uint64_t t59 = gm_l56;
                    uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                    uint64_t t62 = r60;
                    gm_l56 = t62;
                }
            }
gm_ll45:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb41; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc41; }
            }
            goto gm_lad;
lb41:;
        }
    }
    {
        void * t64 = gm_l0;
        void * t65 = GM_LD(void *, (((uint8_t *)t64) + 0));
        if (!(((((uint8_t *)t65) + 960))[4] != 0)) goto ie63;
        uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)t65) + 960));
        gm_l58 = t66;
        {
            void * t68 = (void *)((uint8_t *)&F.l51);
            uint32_t t69 = gm_l58;
            gm_b0_1 t70 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix63;
ie63:;
ix63:;
    }
    {
        void * t71 = (void *)((uint8_t *)&F.l51);
        gm_tqgtd5gtgj65s t72;
        gm_f_drain_leoibma((void *)&t72, (void *)NULL, cx, NULL, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t72))[0] != 0)) {
            gm_tetdkgoreqmn1 t73;
            memset(&t73, 0, sizeof t73);
            ((uint8_t *)&t73)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t73 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t72) + 8)));
            F.ret = t73;
            goto gm_fret;
        }
        gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 8));
    }
    {
        void * t75 = (void *)((uint8_t *)&F.l51);
        gm_t9t4u07j77scv t76;
        gm_f_gen_consts_6ibghrc((void *)&t76, (void *)NULL, cx, NULL, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t76))[0] != 0)) {
            gm_tetdkgoreqmn1 t77;
            memset(&t77, 0, sizeof t77);
            ((uint8_t *)&t77)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t77 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t76) + 8)));
            F.ret = t77;
            goto gm_fret;
        }
        gm_l59 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t76) + 8));
    }
    {
        void * t78 = (void *)((uint8_t *)&F.l51);
        gm_t5o0tpv5jka2l t79;
        gm_f_gen_exports_koai3gr((void *)&t79, (void *)NULL, cx, NULL, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t79))[0] != 0)) {
            gm_tetdkgoreqmn1 t80;
            memset(&t80, 0, sizeof t80);
            ((uint8_t *)&t80)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t80 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t79) + 8)));
            F.ret = t80;
            goto gm_fret;
        }
        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&t79) + 8));
        gm_l60 = t81;
    }
    {
        void * t82 = gm_l0;
        uint8_t t83 = (((GM_LD(uintptr_t, ((((uint8_t *)t82) + 80) + 24) + 8) == 12) && memcmp(GM_LD(void *, ((((uint8_t *)t82) + 80) + 24)), gm_soo4rtckloi4d, 12) == 0));
        gm_l61 = t83;
    }
    {
        uint8_t t85 = gm_l61;
        if (!t85) goto ie84;
        {
            gm_tfeab279c16rr t87;
            void * t88 = (void *)((uint8_t *)&F.l51);
            gm_b16_8 t89 = gm_f_gen_start_ip4boq0((void *)NULL, cx, NULL, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t87 + 0, t89);
            gm_b16_8 t90 = gm_l60;
            GM_ST(gm_b16_8, (uint8_t *)&t87 + 16, t90);
            void * t91 = (void *)((uint8_t *)&F.l51);
            gm_t5o0tpv5jka2l t92;
            gm_f_gen_trap_hook_lu55viv((void *)&t92, (void *)NULL, cx, NULL, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t92))[0] != 0)) {
                gm_tetdkgoreqmn1 t93;
                memset(&t93, 0, sizeof t93);
                ((uint8_t *)&t93)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t93 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t92) + 8)));
                F.ret = t93;
                goto gm_fret;
            }
            gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t87 + 32, t94);
            gm_tabb710d9mq2e t95;
            gm_f_format_dsta2ql((void *)&t95, (void *)NULL, cx, NULL, &t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 8, gm_srcd5arorrf8jkg, 234, 33);
            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&t95) + 8));
            gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
            gm_l60 = t97;
        }
        goto ix84;
ie84:;
ix84:;
    }
    {
        void * t99 = gm_l0;
        uint8_t t100 = (((GM_LD(uintptr_t, ((((uint8_t *)t99) + 80) + 24) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)t99) + 80) + 24)), gm_svl13be5ca8rt, 4) == 0));
        if (!t100) goto ie98;
        void * t101 = gm_l0;
        void * t102 = GM_LD(void *, (((uint8_t *)t101) + 0));
        uint64_t t103 = GM_LD(uintptr_t, ((((uint8_t *)t102) + 880) + 0) + 8);
        uint8_t t104 = (t103 > ((uint64_t)0ULL));
        if (!t104) goto ie98;
        {
            gm_t6j6smj7qoctu t106;
            void * t107 = (void *)((uint8_t *)&F.l51);
            gm_b16_8 t108 = gm_f_gen_start_ip4boq0((void *)NULL, cx, NULL, t107);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t106 + 0, t108);
            gm_b16_8 t109 = gm_l60;
            GM_ST(gm_b16_8, (uint8_t *)&t106 + 16, t109);
            gm_tabb710d9mq2e t110;
            gm_f_format_8n626pi((void *)&t110, (void *)NULL, cx, NULL, &t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 8, gm_srcd5arorrf8jkg, 235, 72);
            gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&t110) + 8));
            gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
            gm_l60 = t112;
        }
        goto ix98;
ie98:;
ix98:;
    }
    {
        gm_b16_8 t113;
        uint8_t t115 = gm_l61;
        if (!t115) goto ie114;
        gm_b16_8 t117;
        GM_ST(void *, (uint8_t *)&t117, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(0));
        gm_b16_8 t118 = t117;
        t113 = t118;
        goto ix114;
ie114:;
        void * t120 = (void *)((uint8_t *)&F.l51);
        gm_b16_8 t121 = gm_l1;
        uint8_t t122 = gm_l2;
        gm_t5o0tpv5jka2l t123;
        gm_f_gen_main_0gsgsok((void *)&t123, (void *)NULL, cx, NULL, t120, t121, t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t123))[0] != 0)) {
            gm_tetdkgoreqmn1 t124;
            memset(&t124, 0, sizeof t124);
            ((uint8_t *)&t124)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t124 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t123) + 8)));
            F.ret = t124;
            goto gm_fret;
        }
        gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 8));
        gm_b16_8 t126 = t125;
        t113 = t126;
ix114:;
        gm_l62 = t113;
    }
    {
        int32_t t127;
        uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        if (!t129) goto ie128;
        int32_t t131 = ((int32_t)1LL);
        t127 = t131;
        goto ix128;
ie128:;
        int32_t t133 = ((int32_t)0LL);
        t127 = t133;
ix128:;
        gm_t19enj90vg8bd t134;
        GM_ST(int32_t, ((uint8_t *)&t134 + 0), t127);
        gm_tabb710d9mq2e t135;
        gm_f_format_7fi363q((void *)&t135, (void *)NULL, cx, NULL, t134);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 8, gm_srcd5arorrf8jkg, 237, 15);
        gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&t135) + 8));
        gm_l63 = t136;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l64, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t137 = (void *)((uint8_t *)&F.l64);
        gm_b16_8 t138 = GM_LD(gm_b16_8, gm_c9e8ns5bad4);
        gm_tftb1odc739t5 t139 = gm_f_extend_0cs85gs(t137, t138);
        if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 1, gm_srcd5arorrf8jkg, 239, 5);
        gm_b0_1 t140 = GM_LD(gm_b0_1, (((uint8_t *)&t139) + 1));
    }
    {
        void * t141 = (void *)((uint8_t *)&F.l64);
        void * t142 = (void *)(((uint8_t *)&F.l51) + 8);
        gm_b16_8 t143 = gm_f_blob_typedefs_4nfn4l8((void *)NULL, cx, NULL, t142);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t144 = gm_f_extend_0cs85gs(t141, t143);
        if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 1, gm_srcd5arorrf8jkg, 240, 5);
        gm_b0_1 t145 = GM_LD(gm_b0_1, (((uint8_t *)&t144) + 1));
    }
    {
        void * t146 = (void *)(((uint8_t *)&F.l51) + 8);
        gm_b16_8 t147 = gm_f_member_names_17el54k((void *)NULL, cx, NULL, t146);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l65 = t147;
    }
    {
        void * t148 = (void *)((uint8_t *)&F.l64);
        gm_b16_8 t149 = gm_l65;
        gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
        void * t151 = (void *)(((uint8_t *)&F.l51) + 8);
        gm_b16_8 t152 = gm_f_mirror_text_cr5muc9(t151);
        gm_b16_8 t153 = gm_f_guard_o7k9a0e((void *)NULL, cx, NULL, t150, t152);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t154 = gm_f_extend_0cs85gs(t148, t153);
        if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 1, gm_srcd5arorrf8jkg, 244, 5);
        gm_b0_1 t155 = GM_LD(gm_b0_1, (((uint8_t *)&t154) + 1));
    }
    {
        void * t156 = (void *)((uint8_t *)&F.l64);
        gm_b16_8 t157 = gm_l65;
        gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
        void * t159 = (void *)(((uint8_t *)&F.l51) + 8);
        gm_b16_8 t160 = gm_f_view_defs_u461okc(t159);
        gm_b16_8 t161 = gm_f_guard_o7k9a0e((void *)NULL, cx, NULL, t158, t160);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t162 = gm_f_extend_0cs85gs(t156, t161);
        if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 1, gm_srcd5arorrf8jkg, 245, 5);
        gm_b0_1 t163 = GM_LD(gm_b0_1, (((uint8_t *)&t162) + 1));
    }
    {
        gm_b16_8 t164;
        uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)a3) + 2));
        if (!t166) goto ie165;
        uint64_t t167 = GM_LD(uintptr_t, (((uint8_t *)a3) + 8) + 8);
        uint8_t t168 = (t167 > ((uint64_t)0ULL));
        if (!t168) goto ie165;
        gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 8));
        gm_b16_8 t171 = gm_f_gdb_script_section_320gurp((void *)NULL, cx, NULL, t170);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t172 = t171;
        t164 = t172;
        goto ix165;
ie165:;
        gm_b16_8 t174;
        GM_ST(void *, (uint8_t *)&t174, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(0));
        gm_b16_8 t175 = t174;
        t164 = t175;
ix165:;
        gm_l66 = t164;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l67, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t176 = (void *)((uint8_t *)&F.l67);
        void * t177 = (void *)(((uint8_t *)&F.l51) + 8);
        gm_b16_8 t178 = gm_f_descriptors_fnnm3h1((void *)NULL, cx, NULL, t177);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t179 = gm_f_extend_0cs85gs(t176, t178);
        if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 1, gm_srcd5arorrf8jkg, 249, 5);
        gm_b0_1 t180 = GM_LD(gm_b0_1, (((uint8_t *)&t179) + 1));
    }
    {
        {
            gm_b16_8 t182 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l51) + 3392) + 0));
            gm_l68 = t182;
        }
        {
            gm_l69 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t184 = gm_l69;
                uint64_t t185 = GM_LD(uintptr_t, ((uint8_t *)&gm_l68) + 8);
                uint8_t t186 = (t184 < t185);
                if (!t186) goto lb183;
                {
                    uint64_t t189 = gm_l69;
                    uint64_t t190 = (uintptr_t)t189;
                    uint64_t t191 = GM_LD(uintptr_t, ((uint8_t *)&gm_l68) + 8);
                    uint32_t t192 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l68)) + t190 * 4));
                    uint32_t t195 = GM_LD(uint32_t, ((uint8_t *)&t192));
                    gm_l70 = t195;
                    {
                        void * t197 = (void *)((uint8_t *)&F.l67);
                        gm_t6j6smj7qoctu t198;
                        void * t199 = (void *)((uint8_t *)&F.l51);
                        uint32_t t200 = gm_l70;
                        gm_b16_8 t201 = gm_f_src_name_0r539f2((void *)NULL, cx, NULL, t199, t200);
                        if (gm_unw.kind) {
                            goto gm_ll187;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t198 + 0, t201);
                        void * t202 = GM_LD(void *, (((uint8_t *)&F.l51) + 0));
                        void * t203 = GM_LD(void *, (((uint8_t *)t202) + 0));
                        void * t204 = GM_LD(void *, (((uint8_t *)t203) + 0));
                        void * t205 = GM_LD(void *, (((uint8_t *)t204) + 0));
                        uint32_t t206 = gm_l70;
                        void * t207 = gm_f_file_o15fqku(t205, t206);
                        gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)t207) + 0));
                        gm_b16_8 t209 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t208);
                        if (gm_unw.kind) {
                            goto gm_ll187;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t198 + 16, t209);
                        gm_tftb1odc739t5 t210 = gm_f_write_hclpi1b(t197, &t198);
                        if (((((uint8_t *)&t210))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t210) + 1, gm_srcd5arorrf8jkg, 251, 9);
                        gm_b0_1 t211 = GM_LD(gm_b0_1, (((uint8_t *)&t210) + 1));
                    }
                    goto me193;
mn194:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 250, 5);
me193:;
                }
lc183:;
                {
                    uint64_t t213 = gm_l69;
                    uint64_t r214; bool o215 = __builtin_add_overflow(t213, ((uint64_t)1ULL), &r214);
                    uint64_t t216 = r214;
                    gm_l69 = t216;
                }
            }
gm_ll187:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb183; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc183; }
            }
            goto gm_lad;
lb183:;
        }
    }
    {
        void * t217 = (void *)((uint8_t *)&F.l67);
        gm_b16_8 t218 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l51) + 1808) + 0));
        gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&t218));
        gm_tftb1odc739t5 t220 = gm_f_extend_0cs85gs(t217, t219);
        if (((((uint8_t *)&t220))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t220) + 1, gm_srcd5arorrf8jkg, 253, 5);
        gm_b0_1 t221 = GM_LD(gm_b0_1, (((uint8_t *)&t220) + 1));
    }
    {
        gm_b16_8 t222 = gm_l65;
        gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
        gm_b16_8 t224 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l51) + 1848) + 0));
        gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&t224));
        gm_b16_8 t226 = gm_f_guard_o7k9a0e((void *)NULL, cx, NULL, t223, t225);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l71 = t226;
    }
    {
        gm_t6j6smj7qoctu t227;
        gm_b16_8 t228 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l51) + 1888) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t227 + 0, t228);
        gm_b16_8 t229 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l51) + 1968) + 0) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t227 + 16, t229);
        gm_tabb710d9mq2e t230;
        gm_f_format_en16lfl((void *)&t230, (void *)NULL, cx, NULL, &t227);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t230))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t230) + 8, gm_srcd5arorrf8jkg, 255, 15);
        gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&t230) + 8));
        gm_l72 = t231;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l73, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t232 = (void *)((uint8_t *)&F.l73);
        gm_b16_8 t233;
        GM_ST(void *, (uint8_t *)&t233, (void *)(gm_s6aq8jknpl0n5));
        GM_ST(uintptr_t, (uint8_t *)&t233 + 8, (uintptr_t)(26));
        gm_tftb1odc739t5 t234 = gm_f_extend_0cs85gs(t232, t233);
        if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 1, gm_srcd5arorrf8jkg, 258, 5);
        gm_b0_1 t235 = GM_LD(gm_b0_1, (((uint8_t *)&t234) + 1));
    }
    {
        void * t236 = (void *)((uint8_t *)&F.l73);
        gm_b16_8 t237 = gm_l63;
        gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&t237));
        gm_tftb1odc739t5 t239 = gm_f_extend_0cs85gs(t236, t238);
        if (((((uint8_t *)&t239))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t239) + 1, gm_srcd5arorrf8jkg, 259, 5);
        gm_b0_1 t240 = GM_LD(gm_b0_1, (((uint8_t *)&t239) + 1));
    }
    {
        void * t241 = (void *)((uint8_t *)&F.l73);
        gm_b16_8 t242 = GM_LD(gm_b16_8, (((uint8_t *)&F.l64) + 0));
        gm_b16_8 t243 = GM_LD(gm_b16_8, ((uint8_t *)&t242));
        gm_tftb1odc739t5 t244 = gm_f_extend_0cs85gs(t241, t243);
        if (((((uint8_t *)&t244))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t244) + 1, gm_srcd5arorrf8jkg, 260, 5);
        gm_b0_1 t245 = GM_LD(gm_b0_1, (((uint8_t *)&t244) + 1));
    }
    {
        void * t246 = (void *)((uint8_t *)&F.l73);
        gm_b16_8 t247 = gm_l66;
        gm_tftb1odc739t5 t248 = gm_f_extend_0cs85gs(t246, t247);
        if (((((uint8_t *)&t248))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t248) + 1, gm_srcd5arorrf8jkg, 261, 5);
        gm_b0_1 t249 = GM_LD(gm_b0_1, (((uint8_t *)&t248) + 1));
    }
    {
        uint64_t t250 = GM_LD(uintptr_t, (((uint8_t *)&F.l73) + 0) + 8);
        F.l74 = t250;
    }
    {
        void * t251 = (void *)((uint8_t *)&F.l73);
        gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)&F.l67) + 0));
        gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)&t252));
        gm_tftb1odc739t5 t254 = gm_f_extend_0cs85gs(t251, t253);
        if (((((uint8_t *)&t254))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t254) + 1, gm_srcd5arorrf8jkg, 263, 5);
        gm_b0_1 t255 = GM_LD(gm_b0_1, (((uint8_t *)&t254) + 1));
    }
    {
        uint64_t t256 = GM_LD(uintptr_t, (((uint8_t *)&F.l73) + 0) + 8);
        F.l75 = t256;
    }
    {
        void * t257 = (void *)((uint8_t *)&F.l73);
        gm_b16_8 t258 = gm_l71;
        gm_tftb1odc739t5 t259 = gm_f_extend_0cs85gs(t257, t258);
        if (((((uint8_t *)&t259))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t259) + 1, gm_srcd5arorrf8jkg, 265, 5);
        gm_b0_1 t260 = GM_LD(gm_b0_1, (((uint8_t *)&t259) + 1));
    }
    {
        void * t261 = (void *)((uint8_t *)&F.l73);
        gm_b16_8 t262 = gm_l72;
        gm_b16_8 t263 = GM_LD(gm_b16_8, ((uint8_t *)&t262));
        gm_tftb1odc739t5 t264 = gm_f_extend_0cs85gs(t261, t263);
        if (((((uint8_t *)&t264))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t264) + 1, gm_srcd5arorrf8jkg, 266, 5);
        gm_b0_1 t265 = GM_LD(gm_b0_1, (((uint8_t *)&t264) + 1));
    }
    {
        void * t266 = (void *)((uint8_t *)&F.l73);
        gm_b16_8 t267 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l51) + 3080) + 0));
        gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&t267));
        gm_tftb1odc739t5 t269 = gm_f_extend_0cs85gs(t266, t268);
        if (((((uint8_t *)&t269))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t269) + 1, gm_srcd5arorrf8jkg, 267, 5);
        gm_b0_1 t270 = GM_LD(gm_b0_1, (((uint8_t *)&t269) + 1));
    }
    {
        void * t271 = (void *)((uint8_t *)&F.l73);
        gm_b16_8 t272 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l59) + 0));
        gm_tftb1odc739t5 t273 = gm_f_extend_0cs85gs(t271, t272);
        if (((((uint8_t *)&t273))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t273) + 1, gm_srcd5arorrf8jkg, 268, 5);
        gm_b0_1 t274 = GM_LD(gm_b0_1, (((uint8_t *)&t273) + 1));
    }
    {
        void * t275 = (void *)((uint8_t *)&F.l73);
        gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l59) + 16));
        gm_tftb1odc739t5 t277 = gm_f_extend_0cs85gs(t275, t276);
        if (((((uint8_t *)&t277))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t277) + 1, gm_srcd5arorrf8jkg, 269, 5);
        gm_b0_1 t278 = GM_LD(gm_b0_1, (((uint8_t *)&t277) + 1));
    }
    {
        void * t279 = (void *)((uint8_t *)&F.l73);
        gm_b16_8 t280 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l51) + 1968) + 40) + 0));
        gm_b16_8 t281 = GM_LD(gm_b16_8, ((uint8_t *)&t280));
        gm_tftb1odc739t5 t282 = gm_f_extend_0cs85gs(t279, t281);
        if (((((uint8_t *)&t282))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t282) + 1, gm_srcd5arorrf8jkg, 270, 5);
        gm_b0_1 t283 = GM_LD(gm_b0_1, (((uint8_t *)&t282) + 1));
    }
    {
        void * t284 = (void *)((uint8_t *)&F.l73);
        gm_b16_8 t285 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l51) + 1928) + 0));
        gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&t285));
        gm_tftb1odc739t5 t287 = gm_f_extend_0cs85gs(t284, t286);
        if (((((uint8_t *)&t287))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t287) + 1, gm_srcd5arorrf8jkg, 271, 5);
        gm_b0_1 t288 = GM_LD(gm_b0_1, (((uint8_t *)&t287) + 1));
    }
    {
        gm_closure t289 = ((gm_closure){(void *)gm_f_code_has_arak3mp, (void *)&F.h, cx});
        gm_l76 = t289;
    }
    {
        void * t291 = gm_l0;
        uint8_t t292 = GM_LD(uint8_t, ((((uint8_t *)t291) + 80) + 57));
        uint8_t t293 = (uint8_t)!t292;
        if (!t293) goto ie290;
        gm_closure t294 = gm_l76;
        gm_b16_8 t295;
        GM_ST(void *, (uint8_t *)&t295, (void *)(gm_scj0sm2kpivk4));
        GM_ST(uintptr_t, (uint8_t *)&t295 + 8, (uintptr_t)(8));
        gm_closure t296 = t294;
        gm_node *ccx297 = t296.cx;
        void * t298 = t296.code;
        uint8_t t299;
        if (GM_PTR_TAGGED(t298)) {
            t299 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t298))(t295);
        } else {
            t299 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t298)(t296.env, ccx297, NULL, t295);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t299) goto ie290;
        {
            gm_tl874t2atnlh3 t301 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
            gm_b16_8 t302;
            GM_ST(void *, (uint8_t *)&t302, (void *)(gm_sdbocdqbukr6k));
            GM_ST(uintptr_t, (uint8_t *)&t302 + 8, (uintptr_t)(76));
            gm_t08olq97rscn3 t303;
            gm_f_unsupported_531q51r((void *)&t303, (void *)NULL, cx, NULL, t301, t302);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            memset(&(*(gm_tetdkgoreqmn1 *)gm_out), 0, sizeof (*(gm_tetdkgoreqmn1 *)gm_out));
            ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 8, t303);
            goto gm_fdone;
        }
        goto ix290;
ie290:;
ix290:;
    }
    {
        uint8_t t305 = gm_l61;
        if (!t305) goto ie304;
        gm_closure t306 = gm_l76;
        gm_b16_8 t307;
        GM_ST(void *, (uint8_t *)&t307, (void *)(gm_sdnjmvrkc1r7r));
        GM_ST(uintptr_t, (uint8_t *)&t307 + 8, (uintptr_t)(11));
        gm_closure t308 = t306;
        gm_node *ccx309 = t308.cx;
        void * t310 = t308.code;
        uint8_t t311;
        if (GM_PTR_TAGGED(t310)) {
            t311 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t310))(t307);
        } else {
            t311 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t310)(t308.env, ccx309, NULL, t307);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t311) goto ie304;
        void * t312 = gm_l0;
        gm_b16_8 t313 = GM_LD(gm_b16_8, ((((uint8_t *)t312) + 80) + 40));
        gm_tfeab279c16rr t314;
        gm_b16_8 t315;
        GM_ST(void *, (uint8_t *)&t315, (void *)(gm_sje4rbc88csdu));
        GM_ST(uintptr_t, (uint8_t *)&t315 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&t314 + 0, t315);
        gm_b16_8 t316;
        GM_ST(void *, (uint8_t *)&t316, (void *)(gm_spervk999ikim));
        GM_ST(uintptr_t, (uint8_t *)&t316 + 8, (uintptr_t)(7));
        GM_ST(gm_b16_8, (uint8_t *)&t314 + 16, t316);
        gm_b16_8 t317;
        GM_ST(void *, (uint8_t *)&t317, (void *)(gm_sqg9do4ma3giu));
        GM_ST(uintptr_t, (uint8_t *)&t317 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t314 + 32, t317);
        uint8_t t318 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t314) + 0, ((uint8_t *)&t313))) t318 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t314) + 16, ((uint8_t *)&t313))) t318 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t314) + 32, ((uint8_t *)&t313))) t318 = 1;
        uint8_t t319 = (uint8_t)(!t318);
        if (!t319) goto ie304;
        {
            gm_t08olq97rscn3 t321;
            memset((uint8_t *)&t321 + 28, 0, 4);
            void * t322 = gm_l0;
            gm_b16_8 t323 = GM_LD(gm_b16_8, ((((uint8_t *)t322) + 80) + 40));
            gm_tnpifv20irnph t324;
            GM_ST(gm_b16_8, ((uint8_t *)&t324 + 0), t323);
            gm_tabb710d9mq2e t325;
            gm_f_format_vj1olp7((void *)&t325, (void *)NULL, cx, NULL, t324);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t325))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t325) + 8, gm_srcd5arorrf8jkg, 279, 32);
            gm_b16_8 t326 = GM_LD(gm_b16_8, (((uint8_t *)&t325) + 8));
            gm_b16_8 t327 = GM_LD(gm_b16_8, ((uint8_t *)&t326));
            GM_ST(gm_b16_8, (uint8_t *)&t321 + 0, t327);
            gm_tl874t2atnlh3 t328 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t321 + 16, t328);
            memset(&(*(gm_tetdkgoreqmn1 *)gm_out), 0, sizeof (*(gm_tetdkgoreqmn1 *)gm_out));
            ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 8, t321);
            goto gm_fdone;
        }
        goto ix304;
ie304:;
ix304:;
    }
    {
        gm_closure t329 = gm_l76;
        gm_b16_8 t330;
        GM_ST(void *, (uint8_t *)&t330, (void *)(gm_sdnjmvrkc1r7r));
        GM_ST(uintptr_t, (uint8_t *)&t330 + 8, (uintptr_t)(11));
        gm_closure t331 = t329;
        gm_node *ccx332 = t331.cx;
        void * t333 = t331.code;
        uint8_t t334;
        if (GM_PTR_TAGGED(t333)) {
            t334 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t333))(t330);
        } else {
            t334 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t333)(t331.env, ccx332, NULL, t330);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l77 = t334;
    }
    {
        gm_b16_8 t335;
        uint8_t t337 = gm_l77;
        if (!t337) goto ie336;
        gm_b16_8 t339;
        GM_ST(void *, (uint8_t *)&t339, (void *)(gm_s5vn66tnlipdf));
        GM_ST(uintptr_t, (uint8_t *)&t339 + 8, (uintptr_t)(18));
        gm_b16_8 t340 = t339;
        t335 = t340;
        goto ix336;
ie336:;
        gm_b16_8 t342;
        GM_ST(void *, (uint8_t *)&t342, (void *)(gm_sr3lr7c9psgf2));
        GM_ST(uintptr_t, (uint8_t *)&t342 + 8, (uintptr_t)(11));
        gm_b16_8 t343 = t342;
        t335 = t343;
ix336:;
        gm_l78 = t335;
    }
    {
        gm_b16_8 t344;
        uint64_t t346 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
        uint8_t t347 = ((t346 == ((uint64_t)0ULL)));
        if (!t347) goto ie345;
        gm_b16_8 t349;
        GM_ST(void *, (uint8_t *)&t349, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t349 + 8, (uintptr_t)(0));
        gm_b16_8 t350 = t349;
        t344 = t350;
        goto ix345;
ie345:;
        gm_t6j6smj7qoctu t352;
        gm_b16_8 t353 = gm_l78;
        GM_ST(gm_b16_8, (uint8_t *)&t352 + 0, t353);
        gm_b16_8 t354 = gm_l62;
        GM_ST(gm_b16_8, (uint8_t *)&t352 + 16, t354);
        gm_tabb710d9mq2e t355;
        gm_f_format_6gtuelb((void *)&t355, (void *)NULL, cx, NULL, &t352);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t355))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t355) + 8, gm_srcd5arorrf8jkg, 284, 56);
        gm_b16_8 t356 = GM_LD(gm_b16_8, (((uint8_t *)&t355) + 8));
        gm_b16_8 t357 = t356;
        gm_b16_8 t358 = GM_LD(gm_b16_8, ((uint8_t *)&t357));
        t344 = t358;
ix345:;
        gm_l79 = t344;
    }
    {
        void * t360 = gm_l0;
        uint8_t t361 = (((GM_LD(uintptr_t, ((((uint8_t *)t360) + 80) + 24) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)t360) + 80) + 24)), gm_svl13be5ca8rt, 4) == 0));
        if (!t361) goto ie359;
        uint64_t t362 = GM_LD(uintptr_t, ((uint8_t *)&gm_l60) + 8);
        uint8_t t363 = (t362 > ((uint64_t)0ULL));
        if (!t363) goto ie359;
        {
            gm_t6j6smj7qoctu t365;
            gm_b16_8 t366;
            uint8_t t368 = gm_l77;
            if (!t368) goto ie367;
            gm_b16_8 t370;
            GM_ST(void *, (uint8_t *)&t370, (void *)(gm_s514u4evks75f));
            GM_ST(uintptr_t, (uint8_t *)&t370 + 8, (uintptr_t)(20));
            gm_b16_8 t371 = t370;
            t366 = t371;
            goto ix367;
ie367:;
            gm_b16_8 t373;
            GM_ST(void *, (uint8_t *)&t373, (void *)(gm_s3nnnhoq632g4));
            GM_ST(uintptr_t, (uint8_t *)&t373 + 8, (uintptr_t)(13));
            gm_b16_8 t374 = t373;
            t366 = t374;
ix367:;
            GM_ST(gm_b16_8, (uint8_t *)&t365 + 0, t366);
            gm_b16_8 t375 = gm_l60;
            GM_ST(gm_b16_8, (uint8_t *)&t365 + 16, t375);
            gm_tabb710d9mq2e t376;
            gm_f_format_eagqd9c((void *)&t376, (void *)NULL, cx, NULL, &t365);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t376))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t376) + 8, gm_srcd5arorrf8jkg, 286, 19);
            gm_b16_8 t377 = GM_LD(gm_b16_8, (((uint8_t *)&t376) + 8));
            gm_b16_8 t378 = GM_LD(gm_b16_8, ((uint8_t *)&t377));
            gm_l60 = t378;
        }
        goto ix359;
ie359:;
ix359:;
    }
    {
        void * t379 = (void *)((uint8_t *)&F.l73);
        gm_b16_8 t380 = gm_l60;
        gm_tftb1odc739t5 t381 = gm_f_extend_0cs85gs(t379, t380);
        if (((((uint8_t *)&t381))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t381) + 1, gm_srcd5arorrf8jkg, 288, 5);
        gm_b0_1 t382 = GM_LD(gm_b0_1, (((uint8_t *)&t381) + 1));
    }
    {
        void * t383 = (void *)((uint8_t *)&F.l73);
        gm_b16_8 t384 = gm_l79;
        gm_tftb1odc739t5 t385 = gm_f_extend_0cs85gs(t383, t384);
        if (((((uint8_t *)&t385))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t385) + 1, gm_srcd5arorrf8jkg, 289, 5);
        gm_b0_1 t386 = GM_LD(gm_b0_1, (((uint8_t *)&t385) + 1));
    }
    {
        gm_b16_8 t387;
        GM_ST(void *, (uint8_t *)&t387, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t387 + 8, (uintptr_t)(0));
        gm_l80 = t387;
    }
    {
        uint8_t t389 = GM_LD(uint8_t, (((uint8_t *)a3) + 2));
        if (!t389) goto ie388;
        {
            gm_b16_8 t391 = GM_LD(gm_b16_8, gm_ccmgjp6th6r);
            gm_tnpifv20irnph t392;
            GM_ST(gm_b16_8, ((uint8_t *)&t392 + 0), t391);
            gm_tabb710d9mq2e t393;
            gm_f_format_uaq30dv((void *)&t393, (void *)NULL, cx, NULL, t392);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t393))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t393) + 8, gm_srcd5arorrf8jkg, 291, 34);
            gm_b16_8 t394 = GM_LD(gm_b16_8, (((uint8_t *)&t393) + 8));
            gm_b16_8 t395 = GM_LD(gm_b16_8, ((uint8_t *)&t394));
            gm_l80 = t395;
        }
        goto ix388;
ie388:;
ix388:;
    }
    {
        gm_b16_8 t396 = GM_LD(gm_b16_8, gm_c9e8ns5bad4);
        gm_l81 = t396;
    }
    {
        gm_tfeab279c16rr t397;
        void * t398 = (void *)(((uint8_t *)&F.l51) + 8);
        gm_b16_8 t399 = gm_f_blob_typedefs_4nfn4l8((void *)NULL, cx, NULL, t398);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t397 + 0, t399);
        void * t400 = (void *)(((uint8_t *)&F.l51) + 8);
        gm_b16_8 t401 = gm_f_mirror_text_cr5muc9(t400);
        GM_ST(gm_b16_8, (uint8_t *)&t397 + 16, t401);
        void * t402 = (void *)(((uint8_t *)&F.l51) + 8);
        gm_b16_8 t403 = gm_f_view_defs_u461okc(t402);
        GM_ST(gm_b16_8, (uint8_t *)&t397 + 32, t403);
        gm_tabb710d9mq2e t404;
        gm_f_format_dsta2ql((void *)&t404, (void *)NULL, cx, NULL, &t397);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t404))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t404) + 8, gm_srcd5arorrf8jkg, 295, 27);
        gm_b16_8 t405 = GM_LD(gm_b16_8, (((uint8_t *)&t404) + 8));
        gm_b16_8 t406 = GM_LD(gm_b16_8, ((uint8_t *)&t405));
        gm_l82 = t406;
    }
    {
        gm_b16_8 t407 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l51) + 1848) + 0));
        gm_b16_8 t408 = GM_LD(gm_b16_8, ((uint8_t *)&t407));
        gm_l83 = t408;
    }
    {
        {
            gm_t6j6smj7qoctu t410;
            gm_b16_8 t411 = GM_LD(gm_b16_8, (((uint8_t *)&F.l64) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t410 + 0, t411);
            gm_b16_8 t412 = gm_l71;
            GM_ST(gm_b16_8, (uint8_t *)&t410 + 16, t412);
            gm_tabb710d9mq2e t413;
            gm_f_format_5029eea((void *)&t413, (void *)NULL, cx, NULL, &t410);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t413))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t413) + 8, gm_srcd5arorrf8jkg, 298, 15);
            gm_b16_8 t414 = GM_LD(gm_b16_8, (((uint8_t *)&t413) + 8));
            gm_b16_8 t415 = GM_LD(gm_b16_8, ((uint8_t *)&t414));
            gm_l84 = t415;
        }
        {
            gm_b16_8 t416 = gm_l72;
            gm_b16_8 t417 = GM_LD(gm_b16_8, ((uint8_t *)&t416));
            gm_l85 = t417;
        }
        {
            gm_tfeab279c16rr t418;
            gm_b16_8 t419 = GM_LD(gm_b16_8, (((uint8_t *)&F.l67) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t418 + 0, t419);
            gm_b16_8 t420 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l51) + 3080) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t418 + 16, t420);
            gm_b16_8 t421 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l59) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t418 + 32, t421);
            gm_tabb710d9mq2e t422;
            gm_f_format_dcns5hm((void *)&t422, (void *)NULL, cx, NULL, &t418);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t422))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t422) + 8, gm_srcd5arorrf8jkg, 300, 15);
            gm_b16_8 t423 = GM_LD(gm_b16_8, (((uint8_t *)&t422) + 8));
            gm_b16_8 t424 = GM_LD(gm_b16_8, ((uint8_t *)&t423));
            gm_l86 = t424;
        }
        {
            gm_t6j6smj7qoctu t425;
            gm_b16_8 t426 = gm_l63;
            GM_ST(gm_b16_8, (uint8_t *)&t425 + 0, t426);
            gm_b16_8 t427 = gm_l66;
            GM_ST(gm_b16_8, (uint8_t *)&t425 + 16, t427);
            gm_tabb710d9mq2e t428;
            gm_f_format_5029eea((void *)&t428, (void *)NULL, cx, NULL, &t425);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t428))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t428) + 8, gm_srcd5arorrf8jkg, 301, 15);
            gm_b16_8 t429 = GM_LD(gm_b16_8, (((uint8_t *)&t428) + 8));
            gm_b16_8 t430 = GM_LD(gm_b16_8, ((uint8_t *)&t429));
            gm_l87 = t430;
        }
        {
            gm_t6j6smj7qoctu t431;
            gm_b16_8 t432 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l51) + 1968) + 40) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t431 + 0, t432);
            gm_b16_8 t433 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l51) + 1928) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t431 + 16, t433);
            gm_tabb710d9mq2e t434;
            gm_f_format_en16lfl((void *)&t434, (void *)NULL, cx, NULL, &t431);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t434))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t434) + 8, gm_srcd5arorrf8jkg, 302, 16);
            gm_b16_8 t435 = GM_LD(gm_b16_8, (((uint8_t *)&t434) + 8));
            gm_b16_8 t436 = GM_LD(gm_b16_8, ((uint8_t *)&t435));
            gm_l88 = t436;
        }
        {
            void * t437 = (void *)((uint8_t *)&F.l51);
            gm_b16_8 t438 = gm_f_owned_d5psnvc((void *)NULL, cx, NULL, t437);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t439 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l51) + 1968) + 40) + 0));
            gm_b16_8 t440 = GM_LD(gm_b16_8, ((uint8_t *)&t439));
            gm_b16_8 t441 = gm_f_with_helpers_edhoqtc((void *)NULL, cx, NULL, t438, t440);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l89 = t441;
        }
        {
            gm_b16_8 t442 = gm_l81;
            gm_l90 = t442;
        }
        {
            gm_b16_8 t443 = gm_l82;
            gm_l91 = t443;
        }
        {
            gm_b16_8 t444 = gm_l83;
            gm_l92 = t444;
        }
        {
            gm_b16_8 t445 = gm_l65;
            gm_b16_8 t446 = GM_LD(gm_b16_8, ((uint8_t *)&t445));
            gm_l93 = t446;
        }
        {
            gm_t0uuhaljsf410 t447;
            gm_b16_8 t448 = gm_l80;
            GM_ST(gm_b16_8, (uint8_t *)&t447 + 0, t448);
            gm_b16_8 t449 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l59) + 16));
            GM_ST(gm_b16_8, (uint8_t *)&t447 + 16, t449);
            gm_b16_8 t450 = gm_l60;
            GM_ST(gm_b16_8, (uint8_t *)&t447 + 32, t450);
            gm_b16_8 t451 = gm_l79;
            GM_ST(gm_b16_8, (uint8_t *)&t447 + 48, t451);
            gm_tabb710d9mq2e t452;
            gm_f_format_k64sjdb((void *)&t452, (void *)NULL, cx, NULL, &t447);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t452))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t452) + 8, gm_srcd5arorrf8jkg, 310, 15);
            gm_b16_8 t453 = GM_LD(gm_b16_8, (((uint8_t *)&t452) + 8));
            gm_b16_8 t454 = GM_LD(gm_b16_8, ((uint8_t *)&t453));
            gm_l94 = t454;
        }
        gm_b16_8 t455 = gm_l84;
        GM_ST(gm_b16_8, (uint8_t *)&F.l95 + 0, t455);
        gm_b16_8 t456 = gm_l85;
        GM_ST(gm_b16_8, (uint8_t *)&F.l95 + 16, t456);
        gm_b16_8 t457 = gm_l86;
        GM_ST(gm_b16_8, (uint8_t *)&F.l95 + 32, t457);
        gm_b16_8 t458 = gm_l87;
        GM_ST(gm_b16_8, (uint8_t *)&F.l95 + 48, t458);
        gm_b16_8 t459 = gm_l88;
        GM_ST(gm_b16_8, (uint8_t *)&F.l95 + 64, t459);
        gm_b16_8 t460 = gm_l94;
        GM_ST(gm_b16_8, (uint8_t *)&F.l95 + 80, t460);
        gm_b16_8 t461 = gm_l89;
        GM_ST(gm_b16_8, (uint8_t *)&F.l95 + 96, t461);
        gm_b16_8 t462 = gm_l90;
        GM_ST(gm_b16_8, (uint8_t *)&F.l95 + 112, t462);
        gm_b16_8 t463 = gm_l91;
        GM_ST(gm_b16_8, (uint8_t *)&F.l95 + 128, t463);
        gm_b16_8 t464 = gm_l92;
        GM_ST(gm_b16_8, (uint8_t *)&F.l95 + 144, t464);
        gm_b16_8 t465 = gm_l93;
        GM_ST(gm_b16_8, (uint8_t *)&F.l95 + 160, t465);
    }
    {
        gm_t0u6dopi4f21f t466;
        memset((uint8_t *)&t466 + 210, 0, 6);
        gm_b16_8 t467 = GM_LD(gm_b16_8, (((uint8_t *)&F.l73) + 0));
        gm_b16_8 t468 = GM_LD(gm_b16_8, ((uint8_t *)&t467));
        GM_ST(gm_b16_8, (uint8_t *)&t466 + 0, t468);
        memcpy((uint8_t *)&t466 + 16, ((uint8_t *)&F.l95), 176);
        gm_b16_8 t469 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l51) + 3240) + 0));
        gm_b16_8 t470 = GM_LD(gm_b16_8, ((uint8_t *)&t469));
        GM_ST(gm_b16_8, (uint8_t *)&t466 + 192, t470);
        uint8_t t471 = gm_l61;
        GM_ST(uint8_t, (uint8_t *)&t466 + 208, t471);
        uint8_t t472 = 0;
        uint8_t t473 = gm_l61;
        if (t473) {
            void * t474 = gm_l0;
            void * t475 = GM_LD(void *, (((uint8_t *)t474) + 0));
            uint8_t t476;
            if ((((((uint8_t *)t475) + 960))[4] != 0)) goto mn478;
            t476 = ((uint8_t)0);
            goto me477;
mn478:;
            t476 = ((uint8_t)1);
            goto me477;
mn479:;
            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 312, 129);
me477:;
            t472 = t476;
        } else t472 = 0;
        GM_ST(uint8_t, (uint8_t *)&t466 + 209, t472);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out))[0] = 0;
        GM_ST(gm_t0u6dopi4f21f, (uint8_t *)&(*(gm_tetdkgoreqmn1 *)gm_out) + 8, t466);
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
extern __attribute__((aligned(4))) void gm_f_generate_object_rgfvem4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
extern __attribute__((aligned(4))) void gm_f_generate_tests_462i0us(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_handler_table_2kdtlit(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_handler_table_2kdtlit F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* h */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* c */
    GM_STACK_CHECK(&F, 543u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 3280);
        uint32_t t5 = gm_l1;
        gm_tqs5p4n2tq19v t6 = gm_f_get_dk5gh28(t4, t5);
        uint8_t t7;
        if (((((uint8_t *)&t6))[1] != 0)) goto mn9;
        t7 = ((uint8_t)1);
        goto me8;
mn9:;
        t7 = ((uint8_t)0);
        goto me8;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 552, 8);
me8:;
        if (!t7) goto ie2;
        {
            void * t12 = gm_l0;
            void * t13 = (void *)(((uint8_t *)t12) + 3280);
            uint32_t t14 = gm_l1;
            gm_tftb1odc739t5 t15 = gm_f_put_v6g5pup(t13, t14, ((uint8_t)1));
            if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srcd5arorrf8jkg, 553, 9);
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        {
            gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t18 = gm_l0;
                void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
                uint32_t t20 = gm_l1;
                void * t21 = gm_f_handler_lit_b02boff(t19, t20);
                gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 24));
                gm_l3 = t22;
            }
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t24 = gm_l4;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t26 = (t24 < t25);
                    if (!t26) goto lb23;
                    {
                        uint64_t t29 = gm_l4;
                        uint64_t t30 = (uintptr_t)t29;
                        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        uint32_t t32 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 4));
                        uint32_t t35 = GM_LD(uint32_t, ((uint8_t *)&t32));
                        gm_l5 = t35;
                        {
                            void * t37 = gm_l0;
                            uint32_t t38 = gm_l5;
                            gm_b0_1 t39 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t37, t38);
                            if (gm_unw.kind) {
                                goto gm_ll27;
                            }
                        }
                        {
                            void * t40 = (void *)((uint8_t *)&F.l2);
                            void * t41 = gm_l0;
                            uint32_t t42 = gm_l5;
                            gm_b16_8 t43 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t41, t42);
                            if (gm_unw.kind) {
                                goto gm_ll27;
                            }
                            gm_tnpifv20irnph t44;
                            GM_ST(gm_b16_8, ((uint8_t *)&t44 + 0), t43);
                            gm_tabb710d9mq2e t45;
                            gm_f_format_be3fee1((void *)&t45, (void *)NULL, cx, NULL, t44);
                            if (gm_unw.kind) {
                                goto gm_ll27;
                            }
                            if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_srcd5arorrf8jkg, 557, 24);
                            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
                            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                            gm_tftb1odc739t5 t48 = gm_f_push_3ef746b(t40, t47);
                            if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srcd5arorrf8jkg, 557, 13);
                            gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                        }
                        goto me33;
mn34:;
                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 555, 9);
me33:;
                    }
lc23:;
                    {
                        uint64_t t51 = gm_l4;
                        uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                        uint64_t t54 = r52;
                        gm_l4 = t54;
                    }
                }
gm_ll27:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb23; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc23; }
                }
                goto gm_lad;
lb23:;
            }
        }
        {
            void * t55 = gm_l0;
            void * t56 = (void *)(((uint8_t *)t55) + 3080);
            gm_t6j6smj7qoctu t57;
            void * t58 = gm_l0;
            uint32_t t59 = gm_l1;
            gm_b16_8 t60 = gm_f_handler_name_6km8n3f((void *)NULL, cx, NULL, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t57 + 0, t60);
            gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t64;
            gm_f_join_sfknljb((void *)&t64, (void *)NULL, cx, NULL, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 8, gm_srcd5arorrf8jkg, 559, 90);
            gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t57 + 16, t65);
            gm_tftb1odc739t5 t66 = gm_f_write_b3knhd6(t56, &t57);
            if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srcd5arorrf8jkg, 559, 9);
            gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t68 = gm_l0;
        uint32_t t69 = gm_l1;
        gm_b16_8 t70 = gm_f_handler_name_6km8n3f((void *)NULL, cx, NULL, t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t71 = 0;
        F.ret = t70;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_qr08i40(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_is_plain_qr08i40 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* def */
    GM_STACK_CHECK(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint32_t t4 = gm_l1;
        void * t5 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t5;
    }
    {
        void * t6 = gm_l2;
        uint8_t t7;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t6) + 96) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma9; default: goto ma12; }
ma9:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t6) + 96) + 0));
        if (t15 != ((uint8_t)3ULL)) goto mn14;
        t7 = ((uint8_t)1);
        goto me8;
mn14:;
ma10:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t6) + 96) + 0));
        if (t19 != ((uint8_t)0ULL)) goto pa18;
        goto po17;
pa18:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t6) + 96) + 0));
        if (t21 != ((uint8_t)1ULL)) goto pa20;
        goto po17;
pa20:;
        goto mn16;
po17:;
        uint8_t t22 = 0;
        void * t23 = gm_l2;
        uint8_t t24;
        if ((((((uint8_t *)t23) + 88))[4] != 0)) goto mn26;
        t24 = ((uint8_t)1);
        goto me25;
mn26:;
        t24 = ((uint8_t)0);
        goto me25;
mn27:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 864, 29);
me25:;
        if (t24) {
            void * t28 = gm_l0;
            void * t29 = gm_l2;
            void * t30 = (void *)(((uint8_t *)t29) + 72);
            uint8_t t31 = gm_f_within_sys_2lc2ugq(t28, t30);
            t22 = t31;
        } else t22 = 0;
        t7 = t22;
        goto me8;
mn16:;
ma11:;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t6) + 96) + 0));
        if (t33 != ((uint8_t)2ULL)) goto mn32;
        t7 = ((uint8_t)0);
        goto me8;
mn32:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 862, 12);
me8:;
        uint8_t t34 = 0;
        F.ret = t7;
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
inline __attribute__((aligned(4))) void gm_f_line_directive_fjg94gc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_line_directive_fjg94gc F;
    void * gm_l0; /* g */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* f */
    gm_th2lqbo77pvsc gm_l3; /* path */
    gm_th2lqbo77pvsc gm_l4; /* cached */
    gm_b16_8 gm_l5; /* abs */
    uint8_t gm_l6; /* stat_failed */
    gm_b16_8 gm_l7; /* p */
    gm_tcc1rq4n1avb2 gm_l8; /* lc */
    GM_STACK_CHECK(&F, 684u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        void * t4 = gm_l0;
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 1136) + 2));
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) {
            gm_tl874t2atnlh3 t7 = gm_l1;
            gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
            uint8_t t9 = (gm_eqff85ac10o3(((uint8_t *)&t7), ((uint8_t *)&t8)));
            t3 = t9;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_th2lqbo77pvsc t11;
            memset(&t11, 0, sizeof t11);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t19 = gm_f_file_o15fqku(t17, t18);
        gm_l2 = t19;
    }
    {
        gm_th2lqbo77pvsc t20;
        memset(&t20, 0, sizeof t20);
        gm_l3 = t20;
    }
    {
        void * t22 = gm_l0;
        void * t23 = (void *)(((uint8_t *)t22) + 3432);
        uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_ti5ramhglnvdt t25;
        gm_f_get_9k2rdij((void *)&t25, t23, t24);
        if (!((((uint8_t *)&t25))[24] != 0)) goto ie21;
        gm_l4 = (*(gm_th2lqbo77pvsc *)(((uint8_t *)&t25) + 0));
        {
            gm_th2lqbo77pvsc t27 = gm_l4;
            gm_l3 = t27;
        }
        goto ix21;
ie21:;
        {
            void * t29 = gm_l2;
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)t29) + 0));
            gm_b16_8 t31 = gm_f_absolute_6pqhsaj((void *)NULL, cx, NULL, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t31;
        }
        {
            gm_b16_8 t32 = gm_l5;
            gm_node *pn33 = gm_find(cx, 2126215853);
            if (!pn33) gm_trap("no handler installed for effect `Fs`", gm_srcd5arorrf8jkg, 525, 24);
            gm_tih6h4avv5nrb t34;
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn33->clauses[5])((void *)&t34, pn33->env, pn33->hctx, pn33, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t35;
            if (!((((uint8_t *)&t34))[0] != 0)) goto mn37;
            t35 = ((uint8_t)1);
            goto me36;
mn37:;
            t35 = ((uint8_t)0);
            goto me36;
mn38:;
            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 525, 24);
me36:;
            gm_l6 = t35;
        }
        {
            uint8_t t40 = gm_l6;
            uint8_t t41 = (uint8_t)!t40;
            if (!t41) goto ie39;
            {
                gm_b16_8 t43 = gm_l5;
                gm_b16_8 t44 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)) + 0, t44);
            }
            goto ix39;
ie39:;
ix39:;
        }
        {
            void * t45 = gm_l0;
            void * t46 = (void *)(((uint8_t *)t45) + 3432);
            uint32_t t47 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
            gm_tftb1odc739t5 t48 = gm_f_put_dm7u1sg(t46, t47, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)));
            if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srcd5arorrf8jkg, 527, 9);
            gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
        }
ix21:;
    }
    {
        gm_th2lqbo77pvsc t50 = gm_l3;
        gm_b16_8 t51;
        if (((((uint8_t *)&t50))[16] != 0)) {
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 0));
            t51 = t52;
        } else {
            gm_th2lqbo77pvsc t53;
            memset(&t53, 0, sizeof t53);
            uint8_t t54 = 0;
            F.ret = t53;
            goto gm_fret;
        }
        gm_l7 = t51;
    }
    {
        void * t55 = gm_l2;
        uint32_t t56 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_tcc1rq4n1avb2 t57 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t55, t56));
        gm_l8 = t57;
    }
    {
        gm_tiilmfd16v3fc t58;
        memset((uint8_t *)&t58 + 4, 0, 4);
        uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
        GM_ST(uint32_t, (uint8_t *)&t58 + 0, t59);
        gm_b16_8 t60 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t58 + 8, t60);
        gm_tabb710d9mq2e t61;
        gm_f_format_v6efj7n((void *)&t61, (void *)NULL, cx, NULL, &t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 8, gm_srcd5arorrf8jkg, 531, 12);
        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t61) + 8));
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t63);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mark_owner_uhhhf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tgqgdb97ka5kt a1) {
    struct gm_fr_mark_owner_uhhhf9f F;
    void * gm_l0; /* g */
    gm_tgqgdb97ka5kt gm_l1; /* owner */
    uint64_t gm_l2; /* n */
    gm_tgqgdb97ka5kt gm_l3; /* last */
    GM_STACK_CHECK(&F, 214u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 1712) + 0) + 8);
        gm_l2 = t3;
    }
    {
        uint64_t t5 = gm_l2;
        uint8_t t6 = (t5 > ((uint64_t)0ULL));
        if (!t6) goto ie4;
        {
            uint64_t t8 = gm_l2;
            uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
            if (o10) gm_trap("integer overflow in `-`", gm_srcd5arorrf8jkg, 425, 37);
            uint64_t t11 = r9;
            uint64_t t12 = (uintptr_t)t11;
            void * t13 = gm_l0;
            uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 1712) + 0) + 8);
            if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srcd5arorrf8jkg, 425, 17);
            gm_tgqgdb97ka5kt t15 = GM_LD(gm_tgqgdb97ka5kt, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 1712) + 0)) + t12 * 24) + 8));
            gm_l3 = t15;
        }
        {
            gm_t9805qtonbsag t17 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l3) + 0));
            gm_t9805qtonbsag t18 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l1) + 0));
            uint8_t t19 = (gm_eqkricceou4u(((uint8_t *)&t17), ((uint8_t *)&t18)));
            if (!t19) goto ie16;
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
            uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t22 = ((t20 == t21));
            if (!t22) goto ie16;
            {
                gm_b0_1 t24;
                uint8_t t25 = 0;
                F.ret = t24;
                goto gm_fret;
            }
            goto ix16;
ie16:;
ix16:;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t26 = gm_l0;
        void * t27 = (void *)(((uint8_t *)t26) + 1712);
        gm_tc42mm7h7sccr t28;
        memset((uint8_t *)&t28 + 20, 0, 4);
        void * t29 = gm_l0;
        uint64_t t30 = GM_LD(uintptr_t, ((((uint8_t *)t29) + 1928) + 0) + 8);
        GM_ST(uint64_t, (uint8_t *)&t28 + 0, t30);
        gm_tgqgdb97ka5kt t31 = gm_l1;
        GM_ST(gm_tgqgdb97ka5kt, (uint8_t *)&t28 + 8, t31);
        gm_tftb1odc739t5 t32 = gm_f_push_cn89s92(t27, &t28);
        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srcd5arorrf8jkg, 428, 5);
        gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_may_unwind_irl2ktm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_may_unwind_irl2ktm F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* def */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* p */
    GM_STACK_CHECK(&F, 325u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint32_t t4 = gm_l1;
        void * t5 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t5;
    }
    {
        void * t6 = gm_l2;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t6) + 96) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma8; default: goto ma11; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t6) + 96) + 0));
        if (t14 != ((uint8_t)3ULL)) goto mn13;
        uint8_t t15 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me7;
mn13:;
ma9:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t6) + 96) + 0));
        if (t19 != ((uint8_t)2ULL)) goto pa18;
        goto po17;
pa18:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t6) + 96) + 0));
        if (t21 != ((uint8_t)1ULL)) goto pa20;
        goto po17;
pa20:;
        goto mn16;
po17:;
        uint8_t t22 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me7;
mn16:;
ma10:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t6) + 96) + 0));
        if (t24 != ((uint8_t)0ULL)) goto mn23;
        {
            void * t27 = gm_l0;
            void * t28 = gm_l2;
            void * t29 = (void *)(((uint8_t *)t28) + 72);
            uint8_t t30 = gm_f_within_sys_2lc2ugq(t27, t29);
            uint8_t t31 = (uint8_t)!t30;
            if (!t31) goto ie26;
            {
                uint8_t t33 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix26;
ie26:;
ix26:;
        }
        {
            {
                void * t35 = gm_l2;
                gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)t35) + 32));
                gm_l3 = t36;
            }
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t38 = gm_l4;
                    uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t40 = (t38 < t39);
                    if (!t40) goto lb37;
                    {
                        uint64_t t43 = gm_l4;
                        uint64_t t44 = (uintptr_t)t43;
                        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        uint32_t t46 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t44 * 4));
                        uint32_t t49 = GM_LD(uint32_t, ((uint8_t *)&t46));
                        gm_l5 = t49;
                        {
                            void * t52 = gm_l0;
                            void * t53 = (void *)(((uint8_t *)t52) + 8);
                            uint32_t t54 = gm_l5;
                            uint64_t t55 = (uint64_t)t54;
                            uint64_t t56 = (uintptr_t)t55;
                            void * t57 = gm_l2;
                            uint64_t t58 = GM_LD(uintptr_t, (((uint8_t *)t57) + 48) + 8);
                            if (t56 >= (uintptr_t)(t58)) gm_trap_index(t56, t58, gm_srcd5arorrf8jkg, 843, 30);
                            gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t57) + 48)) + t56 * 40) + 16));
                            gm_t53at80pfdus3 t60;
                            gm_f_kind_ghvav22((void *)&t60, t53, GM_CONV(gm_csguankko4lmhd, t59));
                            uint8_t t61;
                            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
                            if (t64 != ((uint8_t)15ULL)) goto mn63;
                            t61 = ((uint8_t)1);
                            goto me62;
mn63:;
                            t61 = ((uint8_t)0);
                            goto me62;
mn65:;
                            gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 843, 20);
me62:;
                            if (!t61) goto ie51;
                            {
                                uint8_t t67 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix51;
ie51:;
ix51:;
                        }
                        goto me47;
mn48:;
                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 842, 13);
me47:;
                    }
lc37:;
                    {
                        uint64_t t69 = gm_l4;
                        uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                        uint64_t t72 = r70;
                        gm_l4 = t72;
                    }
                }
lb37:;
            }
        }
        {
            uint8_t t73 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me7;
mn23:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 837, 5);
me7:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_needs_wrapper_ktlbb7i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_needs_wrapper_ktlbb7i F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* def */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* p */
    GM_STACK_CHECK(&F, 281u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint32_t t4 = gm_l1;
        void * t5 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t5;
    }
    {
        uint8_t t7 = 0;
        void * t8 = gm_l2;
        uint8_t t9;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t8) + 96) + 0));
        if (t12 != ((uint8_t)3ULL)) goto mn11;
        t9 = ((uint8_t)1);
        goto me10;
mn11:;
        t9 = ((uint8_t)0);
        goto me10;
mn13:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 879, 8);
me10:;
        if (!t9) {
            void * t14 = gm_l0;
            uint32_t t15 = gm_l1;
            uint8_t t16 = gm_f_is_plain_qr08i40((void *)NULL, cx, NULL, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t17 = (uint8_t)!t16;
            t7 = t17;
        } else t7 = 1;
        if (!t7) goto ie6;
        {
            uint8_t t19 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        {
            void * t21 = gm_l2;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 32));
            gm_l3 = t22;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t24 = gm_l4;
                uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t26 = (t24 < t25);
                if (!t26) goto lb23;
                {
                    uint64_t t29 = gm_l4;
                    uint64_t t30 = (uintptr_t)t29;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint32_t t32 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 4));
                    uint32_t t35 = GM_LD(uint32_t, ((uint8_t *)&t32));
                    gm_l5 = t35;
                    {
                        void * t38 = gm_l0;
                        void * t39 = (void *)(((uint8_t *)t38) + 8);
                        uint32_t t40 = gm_l5;
                        uint64_t t41 = (uint64_t)t40;
                        uint64_t t42 = (uintptr_t)t41;
                        void * t43 = gm_l2;
                        uint64_t t44 = GM_LD(uintptr_t, (((uint8_t *)t43) + 48) + 8);
                        if (t42 >= (uintptr_t)(t44)) gm_trap_index(t42, t44, gm_srcd5arorrf8jkg, 881, 24);
                        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t43) + 48)) + t42 * 40) + 16));
                        uint8_t t46 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t39, t45);
                        if (gm_unw.kind) {
                            goto gm_ll27;
                        }
                        if (!t46) goto ie37;
                        {
                            uint8_t t48 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix37;
ie37:;
ix37:;
                    }
                    goto me33;
mn34:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 880, 5);
me33:;
                }
lc23:;
                {
                    uint64_t t50 = gm_l4;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    uint64_t t53 = r51;
                    gm_l4 = t53;
                }
            }
gm_ll27:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb23; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc23; }
            }
            goto gm_lad;
lb23:;
        }
    }
    {
        void * t54 = gm_l0;
        void * t55 = (void *)(((uint8_t *)t54) + 8);
        void * t56 = gm_l2;
        gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t56) + 64));
        uint8_t t58 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t55, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t59 = 0;
        F.ret = t58;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_owned_d5psnvc(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_owned_d5psnvc F;
    void * gm_l0; /* g */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* k */
    gm_tc42mm7h7sccr gm_l7; /* m */
    uint64_t gm_l8; /* end */
    gm_b16_8 gm_l9; /* path */
    uint32_t gm_l10; /* file */
    uint64_t gm_l11; /* i */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_b16_8 gm_l16; /* p */
    GM_STACK_CHECK(&F, 1277u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_hash_map_lljdp3h((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_lj5vg1g((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 1712) + 0));
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
            gm_l4 = t5;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l5;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    gm_t5c6io15et1sc t12;
                    uint64_t t13 = gm_l5;
                    GM_ST(uint64_t, (uint8_t *)&t12 + 0, t13);
                    uint64_t t14 = gm_l5;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    memcpy((uint8_t *)&t12 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t15 * 24), 24);
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t12) + 0));
                    gm_l6 = t19;
                    gm_l7 = (*(gm_tc42mm7h7sccr *)(((uint8_t *)&t12) + 8));
                    {
                        uint64_t t21;
                        uint64_t t23 = gm_l6;
                        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                        if (o25) gm_trap("integer overflow in `+`", gm_srcd5arorrf8jkg, 438, 19);
                        uint64_t t26 = r24;
                        void * t27 = gm_l0;
                        uint64_t t28 = GM_LD(uintptr_t, ((((uint8_t *)t27) + 1712) + 0) + 8);
                        uint8_t t29 = (t26 < t28);
                        if (!t29) goto ie22;
                        uint64_t t31 = gm_l6;
                        uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                        if (o33) gm_trap("integer overflow in `+`", gm_srcd5arorrf8jkg, 438, 73);
                        uint64_t t34 = r32;
                        uint64_t t35 = (uintptr_t)t34;
                        void * t36 = gm_l0;
                        uint64_t t37 = GM_LD(uintptr_t, ((((uint8_t *)t36) + 1712) + 0) + 8);
                        if (t35 >= (uintptr_t)(t37)) gm_trap_index(t35, t37, gm_srcd5arorrf8jkg, 438, 53);
                        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t36) + 1712) + 0)) + t35 * 24) + 0));
                        uint64_t t39 = t38;
                        t21 = t39;
                        goto ix22;
ie22:;
                        void * t41 = gm_l0;
                        uint64_t t42 = GM_LD(uintptr_t, ((((uint8_t *)t41) + 1928) + 0) + 8);
                        uint64_t t43 = t42;
                        t21 = t43;
ix22:;
                        gm_l8 = t21;
                    }
                    {
                        uint64_t t45 = gm_l8;
                        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
                        uint8_t t47 = (t45 <= t46);
                        if (!t47) goto ie44;
                        {
                            goto lc6;
                        }
                        goto ix44;
ie44:;
ix44:;
                    }
                    {
                        gm_b16_8 t49;
                        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(0));
                        gm_l9 = t49;
                    }
                    {
                        if (!((((((uint8_t *)&gm_l7) + 8) + 0))[4] != 0)) goto ie50;
                        uint32_t t51 = GM_LD(uint32_t, ((((uint8_t *)&gm_l7) + 8) + 0));
                        gm_l10 = t51;
                        {
                            void * t53 = gm_l0;
                            void * t54 = GM_LD(void *, (((uint8_t *)t53) + 0));
                            void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
                            void * t56 = GM_LD(void *, (((uint8_t *)t55) + 0));
                            void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
                            uint32_t t58 = gm_l10;
                            void * t59 = gm_f_file_o15fqku(t57, t58);
                            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)t59) + 0));
                            gm_l9 = t60;
                        }
                        {
                            uint32_t t62 = GM_LD(uint32_t, ((((uint8_t *)&gm_l7) + 8) + 8));
                            uint8_t t63 = (t62 > ((uint32_t)0ULL));
                            if (!t63) goto ie61;
                            {
                                gm_tommf4j9upomc t65;
                                memset((uint8_t *)&t65 + 20, 0, 4);
                                gm_b16_8 t66 = gm_l9;
                                GM_ST(gm_b16_8, (uint8_t *)&t65 + 0, t66);
                                uint32_t t67 = GM_LD(uint32_t, ((((uint8_t *)&gm_l7) + 8) + 8));
                                GM_ST(uint32_t, (uint8_t *)&t65 + 16, t67);
                                gm_tabb710d9mq2e t68;
                                gm_f_format_t3l2mea((void *)&t68, (void *)NULL, cx, NULL, &t65);
                                if (gm_unw.kind) {
                                    goto gm_ll10;
                                }
                                if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_srcd5arorrf8jkg, 443, 43);
                                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                                gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                                gm_l9 = t70;
                            }
                            goto ix61;
ie61:;
ix61:;
                        }
                        goto ix50;
ie50:;
ix50:;
                    }
                    {
                        void * t72 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t73 = gm_l9;
                        gm_tf5srh4mp4b3i t74 = gm_f_get_espvf96(t72, t73);
                        uint8_t t75;
                        if (((((uint8_t *)&t74))[8] != 0)) goto mn77;
                        t75 = ((uint8_t)1);
                        goto me76;
mn77:;
                        t75 = ((uint8_t)0);
                        goto me76;
mn78:;
                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 445, 12);
me76:;
                        if (!t75) goto ie71;
                        {
                            void * t80 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t81 = gm_l9;
                            uint64_t t82 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                            gm_tftb1odc739t5 t83 = gm_f_put_qoojoqv(t80, t81, t82);
                            if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_srcd5arorrf8jkg, 446, 13);
                            gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
                        }
                        {
                            void * t85 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t86 = gm_l9;
                            gm_tftb1odc739t5 t87 = gm_f_push_3ef746b(t85, t86);
                            if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_srcd5arorrf8jkg, 447, 13);
                            gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
                        }
                        {
                            void * t89 = (void *)((uint8_t *)&F.l3);
                            gm_trqfvmjtt4sij t90;
                            gm_f_list_uvfu1k1((void *)&t90, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            gm_tftb1odc739t5 t91 = gm_f_push_97b8r3k(t89, &t90);
                            if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 1, gm_srcd5arorrf8jkg, 448, 13);
                            gm_b0_1 t92 = GM_LD(gm_b0_1, (((uint8_t *)&t91) + 1));
                        }
                        goto ix71;
ie71:;
ix71:;
                    }
                    {
                        void * t93 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t94 = gm_l9;
                        gm_tf5srh4mp4b3i t95 = gm_f_get_espvf96(t93, t94);
                        uint64_t t96;
                        if (((((uint8_t *)&t95))[8] != 0)) {
                            uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&t95) + 0));
                            t96 = t97;
                        } else {
                            gm_trap("reached `@unreachable()`", gm_srcd5arorrf8jkg, 450, 37);
                        }
                        gm_l11 = t96;
                    }
                    {
                        uint64_t t98 = gm_l11;
                        uint64_t t99 = (uintptr_t)t98;
                        uint64_t t100 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        if (t99 >= (uintptr_t)(t100)) gm_trap_index(t99, t100, gm_srcd5arorrf8jkg, 451, 9);
                        void * t101 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t99 * 40);
                        void * t102 = gm_l0;
                        uint64_t t103 = GM_LD(uintptr_t, ((((uint8_t *)t102) + 1928) + 0) + 8);
                        void * t104 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t102) + 1928) + 0)));
                        uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
                        uint64_t t106 = (uintptr_t)t105;
                        uint64_t t107 = gm_l8;
                        uint64_t t108 = (uintptr_t)t107;
                        if (t106 > t108 || t108 > (uintptr_t)t103) gm_trap_slice(t106, t108, t103, gm_srcd5arorrf8jkg, 451, 31);
                        gm_b16_8 t109;
                        GM_ST(void *, (uint8_t *)&t109, (void *)(((uint8_t *)t104 + t106 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(t108 - t106));
                        gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
                        gm_tftb1odc739t5 t111 = gm_f_extend_0cs85gs(t101, t110);
                        if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 1, gm_srcd5arorrf8jkg, 451, 9);
                        gm_b0_1 t112 = GM_LD(gm_b0_1, (((uint8_t *)&t111) + 1));
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 437, 5);
me17:;
                }
lc6:;
                {
                    uint64_t t114 = gm_l5;
                    uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
                    uint64_t t117 = r115;
                    gm_l5 = t117;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
        }
    }
    {
        gm_f_list_av8bg03((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
            gm_l13 = t120;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t122 = gm_l14;
                uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t124 = (t122 < t123);
                if (!t124) goto lb121;
                {
                    gm_tf1bjg81u0glv t127;
                    uint64_t t128 = gm_l14;
                    GM_ST(uint64_t, (uint8_t *)&t127 + 0, t128);
                    uint64_t t129 = gm_l14;
                    uint64_t t130 = (uintptr_t)t129;
                    uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t130 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t127 + 8, t132);
                    uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&t127) + 0));
                    gm_l15 = t135;
                    gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&t127) + 8));
                    gm_l16 = t136;
                    {
                        void * t138 = (void *)((uint8_t *)&F.l12);
                        gm_t6j6smj7qoctu t139;
                        gm_b16_8 t140 = gm_l16;
                        GM_ST(gm_b16_8, (uint8_t *)&t139 + 0, t140);
                        uint64_t t141 = gm_l15;
                        uint64_t t142 = (uintptr_t)t141;
                        uint64_t t143 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        if (t142 >= (uintptr_t)(t143)) gm_trap_index(t142, t143, gm_srcd5arorrf8jkg, 454, 73);
                        gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t142 * 40) + 0));
                        gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
                        GM_ST(gm_b16_8, (uint8_t *)&t139 + 16, t145);
                        gm_tftb1odc739t5 t146 = gm_f_push_lm9fgpm(t138, &t139);
                        if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 1, gm_srcd5arorrf8jkg, 454, 42);
                        gm_b0_1 t147 = GM_LD(gm_b0_1, (((uint8_t *)&t146) + 1));
                    }
                    goto me133;
mn134:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 454, 5);
me133:;
                }
lc121:;
                {
                    uint64_t t149 = gm_l14;
                    uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                    uint64_t t152 = r150;
                    gm_l14 = t152;
                }
            }
lb121:;
        }
    }
    {
        gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
        uint8_t t154 = 0;
        F.ret = t153;
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
inline __attribute__((aligned(4))) gm_tgqgdb97ka5kt gm_f_owner_of_cirm596(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_owner_of_cirm596 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint32_t gm_l2; /* root */
    uint32_t gm_l3; /* p */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint32_t gm_l5; /* group */
    GM_STACK_CHECK(&F, 320u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        gm_l2 = t2;
    }
    {
        for (;;) {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
            uint32_t t6 = gm_l2;
            void * t7 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t5, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!(((((uint8_t *)t7) + 88))[4] != 0)) goto lb3;
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)t7) + 88));
            gm_l3 = t8;
            {
                uint32_t t11 = gm_l3;
                gm_l2 = t11;
            }
lc3:;
        }
lb3:;
    }
    {
        void * t12 = gm_l0;
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
        uint32_t t14 = gm_l2;
        void * t15 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t15) + 16));
        gm_l4 = t16;
    }
    {
        gm_tl874t2atnlh3 t18 = gm_l4;
        gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t20 = (gm_eqff85ac10o3(((uint8_t *)&t18), ((uint8_t *)&t19)));
        if (!t20) goto ie17;
        {
            gm_tgqgdb97ka5kt t22 = GM_LD(gm_tgqgdb97ka5kt, gm_ctvsbqjhdl3);
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        gm_l5 = ((uint32_t)0ULL);
    }
    {
        uint32_t t25 = gm_l2;
        uint64_t t26 = (uint64_t)t25;
        uint64_t t27 = (uintptr_t)t26;
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
        uint64_t t31 = GM_LD(uintptr_t, ((((uint8_t *)t30) + 344) + 0) + 8);
        if (t27 >= (uintptr_t)(t31)) gm_trap_index(t27, t31, gm_srcd5arorrf8jkg, 417, 8);
        uint8_t t32;
        if ((((((uint8_t *)GM_LD(void *, ((((uint8_t *)t30) + 344) + 0)) + t27 * 136) + 96))[24] != 0)) goto mn34;
        t32 = ((uint8_t)0);
        goto me33;
mn34:;
        t32 = ((uint8_t)1);
        goto me33;
mn35:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 417, 8);
me33:;
        if (!t32) goto ie24;
        {
            void * t37 = gm_l0;
            uint32_t t38 = gm_l2;
            gm_b16_8 t39 = gm_f_fn_key_b6f5f24((void *)NULL, cx, NULL, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t40 = gm_f_hash64_apkb7bo(t39);
            uint64_t t41 = GM_LD(uint64_t, gm_cto913mafo2);
            if (t41 == 0) gm_trap("division by zero", gm_srcd5arorrf8jkg, 417, 76);
            uint64_t t42 = (uint64_t)(t40 % t41);
            uint32_t t43 = (uint32_t)t42;
            uint32_t r44; bool o45 = __builtin_add_overflow(((uint32_t)1ULL), t43, &r44);
            if (o45) gm_trap("integer overflow in `+`", gm_srcd5arorrf8jkg, 417, 71);
            uint32_t t46 = r44;
            gm_l5 = t46;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        uint32_t t47 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
        gm_t9805qtonbsag t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t48 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t48), t47);
        ((uint8_t *)&t48)[4] = 1;
        uint32_t t49 = gm_l5;
        gm_tgqgdb97ka5kt t50;
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t50 + 0), t48);
        GM_ST(uint32_t, ((uint8_t *)&t50 + 8), t49);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_queue_cd51r8c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_queue_cd51r8c F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 163u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        gm_l2 = t3;
    }
    {
        for (;;) {
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 2312) + 0) + 8);
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t6 <= t7);
            if (!t8) goto lb4;
            {
                void * t11 = gm_l0;
                void * t12 = (void *)(((uint8_t *)t11) + 2312);
                gm_tftb1odc739t5 t13 = gm_f_push_vd1290p(t12, ((uint8_t)0));
                if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcd5arorrf8jkg, 322, 37);
                gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
            }
lc4:;
        }
lb4:;
    }
    {
        uint64_t t16 = gm_l2;
        uint64_t t17 = (uintptr_t)t16;
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 2312) + 0) + 8);
        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcd5arorrf8jkg, 323, 12);
        uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 2312) + 0)) + t17 * 1));
        uint8_t t21 = (uint8_t)!t20;
        if (!t21) goto ie15;
        {
            uint64_t t23 = gm_l2;
            uint64_t t24 = (uintptr_t)t23;
            void * t25 = gm_l0;
            uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 2312) + 0) + 8);
            if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_srcd5arorrf8jkg, 324, 9);
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t25) + 2312) + 0)) + t24 * 1), ((uint8_t)1));
        }
        {
            void * t27 = gm_l0;
            void * t28 = (void *)(((uint8_t *)t27) + 2272);
            uint32_t t29 = gm_l1;
            gm_tftb1odc739t5 t30 = gm_f_push_im9d4ro(t28, t29);
            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcd5arorrf8jkg, 325, 9);
            gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
        }
        goto ix15;
ie15:;
ix15:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_queue_spec_khngsmv(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_queue_spec_khngsmv F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint32_t gm_l2; /* s */
    GM_STACK_CHECK(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 2688);
        uint32_t t5 = gm_l1;
        uint32_t t6 = gm_l2;
        gm_tcc1rq4n1avb2 t7;
        GM_ST(uint32_t, ((uint8_t *)&t7 + 0), t5);
        GM_ST(uint32_t, ((uint8_t *)&t7 + 4), t6);
        gm_tqs5p4n2tq19v t8 = gm_f_get_ag7vhtq(t4, t7);
        uint8_t t9;
        if (((((uint8_t *)&t8))[1] != 0)) goto mn11;
        t9 = ((uint8_t)1);
        goto me10;
mn11:;
        t9 = ((uint8_t)0);
        goto me10;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 331, 8);
me10:;
        if (!t9) goto ie2;
        {
            void * t14 = gm_l0;
            void * t15 = (void *)(((uint8_t *)t14) + 2688);
            uint32_t t16 = gm_l1;
            uint32_t t17 = gm_l2;
            gm_tcc1rq4n1avb2 t18;
            GM_ST(uint32_t, ((uint8_t *)&t18 + 0), t16);
            GM_ST(uint32_t, ((uint8_t *)&t18 + 4), t17);
            gm_tftb1odc739t5 t19 = gm_f_put_3g1tdpr(t15, t18, ((uint8_t)1));
            if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 1, gm_srcd5arorrf8jkg, 332, 9);
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 1));
        }
        {
            void * t21 = gm_l0;
            void * t22 = (void *)(((uint8_t *)t21) + 2744);
            uint32_t t23 = gm_l1;
            uint32_t t24 = gm_l2;
            gm_tcc1rq4n1avb2 t25;
            GM_ST(uint32_t, ((uint8_t *)&t25 + 0), t23);
            GM_ST(uint32_t, ((uint8_t *)&t25 + 4), t24);
            gm_tftb1odc739t5 t26 = gm_f_push_sgikgd8(t22, t25);
            if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srcd5arorrf8jkg, 333, 9);
            gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t28 = gm_l0;
        uint32_t t29 = gm_l1;
        uint32_t t30 = gm_l2;
        gm_b16_8 t31 = gm_f_spec_name_jptsbbf((void *)NULL, cx, NULL, t28, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t32 = 0;
        F.ret = t31;
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
inline __attribute__((aligned(4))) uint32_t gm_f_spec_bitccmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tf9f5m02f56gm a1) {
    struct gm_fr_spec_bitccmf F;
    void * gm_l0; /* g */
    gm_tf9f5m02f56gm gm_l1; /* key */
    gm_tl874t2atnlh3 gm_l2; /* k */
    uint32_t gm_l3; /* s */
    uint32_t gm_l4; /* s */
    GM_STACK_CHECK(&F, 222u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_t9805qtonbsag t4 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l1) + 8));
        uint32_t t5 = gm_f_spec_slot_jild38d(t4);
        gm_tl874t2atnlh3 t6;
        GM_ST(uint32_t, ((uint8_t *)&t6 + 0), t2);
        GM_ST(uint32_t, ((uint8_t *)&t6 + 4), t3);
        GM_ST(uint32_t, ((uint8_t *)&t6 + 8), t5);
        gm_l2 = t6;
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 2632);
        gm_tl874t2atnlh3 t10 = gm_l2;
        gm_t9805qtonbsag t11 = gm_f_get_pftnkei(t9, t10);
        if (!((((uint8_t *)&t11))[4] != 0)) goto ie7;
        uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)&t11));
        gm_l3 = t12;
        {
            uint32_t t14 = gm_l3;
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 2592) + 0) + 8);
        uint32_t t18 = (uint32_t)t17;
        gm_l4 = t18;
    }
    {
        void * t19 = gm_l0;
        void * t20 = (void *)(((uint8_t *)t19) + 2592);
        gm_tf9f5m02f56gm t21 = gm_l1;
        gm_tftb1odc739t5 t22 = gm_f_push_jttnn67(t20, t21);
        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srcd5arorrf8jkg, 343, 5);
        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
    }
    {
        void * t24 = gm_l0;
        void * t25 = (void *)(((uint8_t *)t24) + 2632);
        gm_tl874t2atnlh3 t26 = gm_l2;
        uint32_t t27 = gm_l4;
        gm_tftb1odc739t5 t28 = gm_f_put_bii31he(t25, t26, t27);
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srcd5arorrf8jkg, 344, 5);
        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
    }
    {
        uint32_t t30 = gm_l4;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint32_t gm_f_spec_depth_em7r6vt(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_handler_table_8h552cp(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_spec_handler_table_8h552cp F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* h */
    uint32_t gm_l2; /* s */
    gm_b16_8 gm_l3; /* name */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l7; /* c */
    GM_STACK_CHECK(&F, 715u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_t6j6smj7qoctu t2;
        void * t3 = gm_l0;
        uint32_t t4 = gm_l1;
        gm_b16_8 t5 = gm_f_handler_name_6km8n3f((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t5);
        void * t6 = gm_l0;
        uint32_t t7 = gm_l2;
        gm_b16_8 t8 = gm_f_spec_text_iq7r8vn((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t9 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t8, ((uint64_t)6ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t9);
        gm_tabb710d9mq2e t10;
        gm_f_format_kj13ck1((void *)&t10, (void *)NULL, cx, NULL, &t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_srcd5arorrf8jkg, 538, 13);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
        gm_l3 = t11;
    }
    {
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 2784);
        uint32_t t15 = gm_l1;
        uint32_t t16 = gm_l2;
        gm_tcc1rq4n1avb2 t17;
        GM_ST(uint32_t, ((uint8_t *)&t17 + 0), t15);
        GM_ST(uint32_t, ((uint8_t *)&t17 + 4), t16);
        gm_tqs5p4n2tq19v t18 = gm_f_get_ag7vhtq(t14, t17);
        uint8_t t19;
        if (((((uint8_t *)&t18))[1] != 0)) goto mn21;
        t19 = ((uint8_t)1);
        goto me20;
mn21:;
        t19 = ((uint8_t)0);
        goto me20;
mn22:;
        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 539, 8);
me20:;
        if (!t19) goto ie12;
        {
            void * t24 = gm_l0;
            void * t25 = (void *)(((uint8_t *)t24) + 2784);
            uint32_t t26 = gm_l1;
            uint32_t t27 = gm_l2;
            gm_tcc1rq4n1avb2 t28;
            GM_ST(uint32_t, ((uint8_t *)&t28 + 0), t26);
            GM_ST(uint32_t, ((uint8_t *)&t28 + 4), t27);
            gm_tftb1odc739t5 t29 = gm_f_put_3g1tdpr(t25, t28, ((uint8_t)1));
            if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srcd5arorrf8jkg, 540, 9);
            gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
        }
        {
            gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t32 = gm_l0;
                void * t33 = GM_LD(void *, (((uint8_t *)t32) + 0));
                uint32_t t34 = gm_l1;
                void * t35 = gm_f_handler_lit_b02boff(t33, t34);
                gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)t35) + 24));
                gm_l5 = t36;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t38 = gm_l6;
                    uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t40 = (t38 < t39);
                    if (!t40) goto lb37;
                    {
                        uint64_t t43 = gm_l6;
                        uint64_t t44 = (uintptr_t)t43;
                        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        uint32_t t46 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t44 * 4));
                        uint32_t t49 = GM_LD(uint32_t, ((uint8_t *)&t46));
                        gm_l7 = t49;
                        {
                            void * t51 = (void *)((uint8_t *)&F.l4);
                            void * t52 = gm_l0;
                            uint32_t t53 = gm_l7;
                            uint32_t t54 = gm_l2;
                            gm_b16_8 t55 = gm_f_queue_spec_khngsmv((void *)NULL, cx, NULL, t52, t53, t54);
                            if (gm_unw.kind) {
                                goto gm_ll41;
                            }
                            gm_tnpifv20irnph t56;
                            GM_ST(gm_b16_8, ((uint8_t *)&t56 + 0), t55);
                            gm_tabb710d9mq2e t57;
                            gm_f_format_be3fee1((void *)&t57, (void *)NULL, cx, NULL, t56);
                            if (gm_unw.kind) {
                                goto gm_ll41;
                            }
                            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 8, gm_srcd5arorrf8jkg, 543, 24);
                            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t57) + 8));
                            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                            gm_tftb1odc739t5 t60 = gm_f_push_3ef746b(t51, t59);
                            if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_srcd5arorrf8jkg, 543, 13);
                            gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
                        }
                        goto me47;
mn48:;
                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 542, 9);
me47:;
                    }
lc37:;
                    {
                        uint64_t t63 = gm_l6;
                        uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                        uint64_t t66 = r64;
                        gm_l6 = t66;
                    }
                }
gm_ll41:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb37; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc37; }
                }
                goto gm_lad;
lb37:;
            }
        }
        {
            void * t67 = gm_l0;
            void * t68 = (void *)(((uint8_t *)t67) + 3080);
            gm_t6j6smj7qoctu t69;
            gm_b16_8 t70 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t69 + 0, t70);
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t74;
            gm_f_join_sfknljb((void *)&t74, (void *)NULL, cx, NULL, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 8, gm_srcd5arorrf8jkg, 545, 77);
            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t69 + 16, t75);
            gm_tftb1odc739t5 t76 = gm_f_write_pfrpr5k(t68, &t69);
            if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcd5arorrf8jkg, 545, 9);
            gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        gm_b16_8 t78 = gm_l3;
        gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
        uint8_t t80 = 0;
        F.ret = t79;
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
extern __attribute__((aligned(4))) gm_tf9f5m02f56gm gm_f_spec_key_eljlge7(void * a0, uint32_t a1);
extern __attribute__((aligned(4))) uint32_t gm_f_spec_slot_jild38d(gm_t9805qtonbsag a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_src_loc_lrhtj7i F;
    void * gm_l0; /* g */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* f */
    gm_tcc1rq4n1avb2 gm_l3; /* lc */
    GM_STACK_CHECK(&F, 351u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t5 = (gm_eqff85ac10o3(((uint8_t *)&t3), ((uint8_t *)&t4)));
        if (!t5) goto ie2;
        {
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sp5lvq92b4in0));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(10));
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t15 = gm_f_file_o15fqku(t13, t14);
        gm_l2 = t15;
    }
    {
        void * t16 = gm_l2;
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_tcc1rq4n1avb2 t18 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t16, t17));
        gm_l3 = t18;
    }
    {
        uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t21 = gm_l0;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)t21) + 3392) + 0));
        uint8_t t23 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t22) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t22))) + i * 4, ((uint8_t *)&t20))) { t23 = 1; break; }
        uint8_t t24 = (uint8_t)(!t23);
        if (!t24) goto ie19;
        {
            void * t26 = gm_l0;
            void * t27 = (void *)(((uint8_t *)t26) + 3392);
            uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
            gm_tftb1odc739t5 t29 = gm_f_push_im9d4ro(t27, t28);
            if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srcd5arorrf8jkg, 507, 45);
            gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        gm_t5hdnnftkup6m t31;
        void * t32 = gm_l0;
        uint32_t t33 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_b16_8 t34 = gm_f_src_name_0r539f2((void *)NULL, cx, NULL, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t31 + 0, t34);
        uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
        GM_ST(uint32_t, (uint8_t *)&t31 + 16, t35);
        uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        GM_ST(uint32_t, (uint8_t *)&t31 + 20, t36);
        gm_tabb710d9mq2e t37;
        gm_f_format_6g8g0f7((void *)&t37, (void *)NULL, cx, NULL, &t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_srcd5arorrf8jkg, 508, 12);
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
        uint8_t t40 = 0;
        F.ret = t39;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_src_name_0r539f2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_static_bytes_u0uvco3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_static_bytes_u0uvco3 F;
    void * gm_l0; /* g */
    gm_b16_8 gm_l1; /* bytes */
    gm_b16_8 gm_l2; /* n */
    gm_b16_8 gm_l3; /* n */
    GM_STACK_CHECK(&F, 414u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 1752);
        gm_b16_8 t5 = gm_l1;
        gm_th2lqbo77pvsc t6;
        gm_f_get_e3joqcc((void *)&t6, t4, t5);
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l2 = t7;
        {
            gm_b16_8 t9 = gm_l2;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s8rsp9val5ful));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(4));
        gm_b16_8 t12 = gm_l1;
        gm_b16_8 t13 = gm_f_content_name_83mn5uf((void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t13;
    }
    {
        void * t14 = gm_l0;
        void * t15 = (void *)(((uint8_t *)t14) + 1752);
        gm_b16_8 t16 = gm_l1;
        gm_b16_8 t17 = gm_l3;
        gm_tftb1odc739t5 t18 = gm_f_put_7shfu9u(t15, t16, t17);
        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_srcd5arorrf8jkg, 497, 5);
        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
    }
    {
        void * t20 = gm_l0;
        void * t21 = (void *)(((uint8_t *)t20) + 1808);
        gm_tq3m5qgvkpje8 t22;
        gm_b16_8 t23 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t22 + 0, t23);
        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
        if (o26) gm_trap("integer overflow in `+`", gm_srcd5arorrf8jkg, 498, 67);
        uint64_t t27 = r25;
        GM_ST(uint64_t, (uint8_t *)&t22 + 16, t27);
        gm_b16_8 t28 = gm_l1;
        gm_b16_8 t29 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t22 + 24, t29);
        gm_tftb1odc739t5 t30 = gm_f_write_uuia5cm(t21, &t22);
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcd5arorrf8jkg, 498, 5);
        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
    }
    {
        gm_b16_8 t32 = gm_l3;
        uint8_t t33 = 0;
        F.ret = t32;
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
inline __attribute__((aligned(4))) uint8_t gm_f_takes_fn_n239ee5(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_takes_fn_n239ee5 F;
    void * gm_l0; /* g */
    gm_b16_8 gm_l1; /* params */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t88rcucg1b7uk gm_l4; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 211u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_b16_8 t3 = gm_l1;
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
                    gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 4));
                    gm_l4 = t15;
                    {
                        void * t18 = gm_l0;
                        void * t19 = (void *)(((uint8_t *)t18) + 8);
                        gm_t88rcucg1b7uk t20 = gm_l4;
                        gm_t53at80pfdus3 t21;
                        gm_f_kind_ghvav22((void *)&t21, t19, GM_CONV(gm_csguankko4lmhd, t20));
                        uint8_t t22;
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
                        if (t25 != ((uint8_t)15ULL)) goto mn24;
                        t22 = ((uint8_t)1);
                        goto me23;
mn24:;
                        t22 = ((uint8_t)0);
                        goto me23;
mn26:;
                        gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 929, 12);
me23:;
                        if (!t22) goto ie17;
                        {
                            uint8_t t28 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 928, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t30 = gm_l3;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l3 = t33;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t34 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_with_helpers_edhoqtc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_with_helpers_edhoqtc F;
    gm_b16_8 gm_l0; /* owned */
    gm_b16_8 gm_l1; /* helpers */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    gm_t6j6smj7qoctu gm_l5; /* o */
    GM_STACK_CHECK(&F, 616u);
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
                    gm_t1l3r16b8478v t11;
                    uint64_t t12 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t11 + 0, t12);
                    uint64_t t13 = gm_l3;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    memcpy((uint8_t *)&t11 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t14 * 32), 32);
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t11) + 0));
                    gm_l4 = t18;
                    gm_l5 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t11) + 8));
                    {
                        uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)&gm_l5) + 0) + 8);
                        uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
                        if (!t22) goto ie20;
                        {
                            uint64_t t24 = gm_l4;
                            uint64_t t25 = (uintptr_t)t24;
                            uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srcd5arorrf8jkg, 462, 13);
                            uint8_t *t27 = (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t25 * 32) + 16);
                            gm_t6j6smj7qoctu t28;
                            gm_b16_8 t29 = gm_l1;
                            GM_ST(gm_b16_8, (uint8_t *)&t28 + 0, t29);
                            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
                            GM_ST(gm_b16_8, (uint8_t *)&t28 + 16, t30);
                            gm_tabb710d9mq2e t31;
                            gm_f_format_8n626pi((void *)&t31, (void *)NULL, cx, NULL, &t28);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_srcd5arorrf8jkg, 462, 29);
                            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
                            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                            GM_ST(gm_b16_8, t27, t33);
                        }
                        {
                            gm_b16_8 t34 = gm_l0;
                            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
                            uint8_t t36 = 0;
                            F.ret = t35;
                            goto gm_fret;
                        }
                        goto ix20;
ie20:;
ix20:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 460, 5);
me16:;
                }
lc5:;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
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
        gm_f_list_av8bg03((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t42 = (void *)((uint8_t *)&F.l6);
        gm_t6j6smj7qoctu t43;
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(0));
        GM_ST(gm_b16_8, (uint8_t *)&t43 + 0, t44);
        gm_b16_8 t45 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t43 + 16, t45);
        gm_tftb1odc739t5 t46 = gm_f_push_lm9fgpm(t42, &t43);
        if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_srcd5arorrf8jkg, 467, 5);
        gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
    }
    {
        void * t48 = (void *)((uint8_t *)&F.l6);
        gm_b16_8 t49 = gm_l0;
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        gm_tftb1odc739t5 t51 = gm_f_extend_msip1bg(t48, t50);
        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srcd5arorrf8jkg, 468, 5);
        gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
    }
    {
        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
        uint8_t t55 = 0;
        F.ret = t54;
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
inline __attribute__((aligned(4))) uint8_t gm_f_within_sys_2lc2ugq(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_within_sys_2lc2ugq F;
    void * gm_l0; /* g */
    void * gm_l1; /* set */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tiilmfd16v3fc gm_l4; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 210u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
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
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tiilmfd16v3fc t16 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 24));
                    gm_l4 = t16;
                    {
                        void * t19 = gm_l0;
                        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
                        gm_t9805qtonbsag t21 = GM_LD(gm_t9805qtonbsag, ((((uint8_t *)t20) + 48) + 24));
                        uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
                        gm_t9805qtonbsag t23;
                        GM_ST(uint8_t, (uint8_t *)&t23 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t23 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t23), t22);
                        ((uint8_t *)&t23)[4] = 1;
                        uint8_t t24 = (!gm_eqkricceou4u(((uint8_t *)&t21), ((uint8_t *)&t23)));
                        if (!t24) goto ie18;
                        {
                            uint8_t t26 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcd5arorrf8jkg, 852, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t28 = gm_l3;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l3 = t31;
                }
            }
lb5:;
        }
    }
    {
        uint8_t t32 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
