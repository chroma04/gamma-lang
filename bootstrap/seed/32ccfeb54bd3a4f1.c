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
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tammvh1iespi3;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tammvh1iespi3) == 56 && _Alignof(gm_tammvh1iespi3) == 8 && offsetof(gm_tammvh1iespi3, f0) == 0 && offsetof(gm_tammvh1iespi3, f1) == 16 && offsetof(gm_tammvh1iespi3, f2) == 24 && offsetof(gm_tammvh1iespi3, f3) == 40, "layout of `EffectInfo`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_tiilmfd16v3fc f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tj44oivvogvtl;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t pe[4]; } gm_tuanp0jbrlb6j;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tj44oivvogvtl) == 40 && _Alignof(gm_tj44oivvogvtl) == 8 && offsetof(gm_tj44oivvogvtl, f0) == 0 && offsetof(gm_tj44oivvogvtl, f1) == 24, "layout of `Performed`");
typedef struct { gm_tuanp0jbrlb6j f0; uint8_t f1; uint8_t pe[7]; } gm_tjvtgq0p0sehv;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tuanp0jbrlb6j) == 32 && _Alignof(gm_tuanp0jbrlb6j) == 8 && offsetof(gm_tuanp0jbrlb6j, f0) == 0 && offsetof(gm_tuanp0jbrlb6j, f1) == 24, "layout of `ForLabel`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tjvtgq0p0sehv) == 40 && _Alignof(gm_tjvtgq0p0sehv) == 8 && offsetof(gm_tjvtgq0p0sehv, f0) == 0 && offsetof(gm_tjvtgq0p0sehv, f1) == 32, "layout of `?ForLabel`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_tkn42u7na3t81 f3; uint8_t p4[4]; gm_tjvtgq0p0sehv f4; gm_tkn42u7na3t81 f5; gm_tqu3m2l3n8mnc f6; } gm_tnqmlaad6uchl;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_tnqmlaad6uchl f0; uint8_t f1; uint8_t pe[7]; } gm_t403eokj7ifg9;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_tnqmlaad6uchl) == 112 && _Alignof(gm_tnqmlaad6uchl) == 8 && offsetof(gm_tnqmlaad6uchl, f0) == 0 && offsetof(gm_tnqmlaad6uchl, f1) == 4 && offsetof(gm_tnqmlaad6uchl, f2) == 12 && offsetof(gm_tnqmlaad6uchl, f3) == 20 && offsetof(gm_tnqmlaad6uchl, f4) == 32 && offsetof(gm_tnqmlaad6uchl, f5) == 72 && offsetof(gm_tnqmlaad6uchl, f6) == 80, "layout of `ClauseInfo`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_t403eokj7ifg9) == 120 && _Alignof(gm_t403eokj7ifg9) == 8 && offsetof(gm_t403eokj7ifg9, f0) == 0 && offsetof(gm_t403eokj7ifg9, f1) == 112, "layout of `?ClauseInfo`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_trqfvmjtt4sij f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; uint32_t f5; uint8_t f6; uint8_t p7[3]; uint64_t f7; gm_t9805qtonbsag f8; uint8_t f9; uint8_t f10; uint8_t p11[2]; gm_tkn42u7na3t81 f11; uint8_t p12[4]; gm_t403eokj7ifg9 f12; gm_trqfvmjtt4sij f13; gm_trqfvmjtt4sij f14; } gm_tjhr0bdm69ios;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_tjhr0bdm69ios) == 384 && _Alignof(gm_tjhr0bdm69ios) == 8 && offsetof(gm_tjhr0bdm69ios, f0) == 0 && offsetof(gm_tjhr0bdm69ios, f1) == 40 && offsetof(gm_tjhr0bdm69ios, f2) == 48 && offsetof(gm_tjhr0bdm69ios, f3) == 64 && offsetof(gm_tjhr0bdm69ios, f4) == 104 && offsetof(gm_tjhr0bdm69ios, f5) == 144 && offsetof(gm_tjhr0bdm69ios, f6) == 148 && offsetof(gm_tjhr0bdm69ios, f7) == 152 && offsetof(gm_tjhr0bdm69ios, f8) == 160 && offsetof(gm_tjhr0bdm69ios, f9) == 168 && offsetof(gm_tjhr0bdm69ios, f10) == 169 && offsetof(gm_tjhr0bdm69ios, f11) == 172 && offsetof(gm_tjhr0bdm69ios, f12) == 184 && offsetof(gm_tjhr0bdm69ios, f13) == 304 && offsetof(gm_tjhr0bdm69ios, f14) == 344, "layout of `Frame`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_add_4m1441i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_available_7okjiog {
    gm_frame h;
    uint32_t l1; /* eff */
    gm_trqfvmjtt4sij l2; /* out */
    gm_tjhr0bdm69ios l6; /* f */
    gm_b16_8 ret;
};
struct gm_fr_effect_map_b2f0sd4 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8fnsr5h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
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
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_oalq03d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vmvl81b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_perform_bound_cc96od0 {
    gm_frame h;
    void * ret;
};
struct gm_fr_perform_op_1nih4r3 {
    gm_frame h;
    gm_tammvh1iespi3 l5; /* info */
    gm_tv0k2vg3054a6 l6; /* o */
    gm_trqfvmjtt4sij l11; /* binds */
    gm_trqfvmjtt4sij l12; /* probe */
    gm_t7te2tdpebbt4 l16; /* a */
    gm_tc3oht4ksth9d l17; /* e */
    gm_trqfvmjtt4sij l23; /* gargs */
    gm_trqfvmjtt4sij l33; /* vals */
    void * ret;
};
struct gm_fr_perform_op_as_9ncunsc {
    gm_frame h;
    gm_tammvh1iespi3 l5; /* info */
    gm_tv0k2vg3054a6 l6; /* o */
    gm_trqfvmjtt4sij l8; /* ps */
    gm_t5v0c8ia6a5mo l11; /* p */
    gm_t5v0c8ia6a5mo l12; /* q */
    void * ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
__attribute__((aligned(4))) gm_b0_1 gm_f_add_4m1441i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_available_7okjiog(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_bind_args_dnih8ih(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bound_to_5c11t0h(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_effect_map_b2f0sd4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) void * gm_f_finish_call_kjknkfg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_format_5qcf83g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9nalvct(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ql3u2no(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_match_params_6vndkud(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, void * a3);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_perform_bound_cc96od0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2, gm_t88rcucg1b7uk a3, const void * a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_perform_op_1nih4r3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_perform_op_as_9ncunsc(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3onuaru(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern const uint8_t gm_sbiv1tnsmo6hr[35];
extern const uint8_t gm_seiuio4eghuvu[19];
extern const uint8_t gm_sk6ueggk5ji6v[10];
extern const uint8_t gm_snch62us408pn[22];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srct4pcukr5kqhq[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8fnsr5h F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_af74s0q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_he774nl();
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
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_oalq03d F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8fnsr5h();
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_g3tddrb_2();
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_4m1441i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_add_4m1441i F;
    gm_b16_8 gm_l0; /* set */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_tiilmfd16v3fc gm_l3; /* e */
    uint8_t gm_l4; /* seen */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tiilmfd16v3fc gm_l7; /* x */
    GM_STACK_CHECK(&F, 351u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l1 = t3;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l2;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_tiilmfd16v3fc t15 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t11 * 24));
                    gm_l3 = t15;
                    {
                        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                        uint32_t t19 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_available_7okjiog *)((&F.h)->up))->l1));
                        uint8_t t20 = (!(t18 == t19));
                        if (!t20) goto ie17;
                        {
                            goto lc4;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    {
                        gm_l4 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&((struct gm_fr_available_7okjiog *)((&F.h)->up))->l2) + 0));
                            gm_l5 = t23;
                        }
                        {
                            gm_l6 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t25 = gm_l6;
                                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                uint8_t t27 = (t25 < t26);
                                if (!t27) goto lb24;
                                {
                                    uint64_t t30 = gm_l6;
                                    uint64_t t31 = (uintptr_t)t30;
                                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                    gm_tiilmfd16v3fc t35 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t31 * 24));
                                    gm_l7 = t35;
                                    {
                                        void * t38 = (void *)((uint8_t *)&gm_l7);
                                        void * t39 = (void *)((uint8_t *)&gm_l3);
                                        int32_t t40 = gm_f_cmp_ref_8ntrqll(t38, t39);
                                        uint8_t t41 = ((t40 == ((int32_t)0LL)));
                                        if (!t41) goto ie37;
                                        {
                                            gm_l4 = ((uint8_t)1);
                                        }
                                        goto ix37;
ie37:;
ix37:;
                                    }
                                    goto me33;
mn34:;
                                    gm_trap("no match arm matched the scrutinee", gm_srct4pcukr5kqhq, 12, 13);
me33:;
                                }
lc24:;
                                {
                                    uint64_t t44 = gm_l6;
                                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                                    uint64_t t47 = r45;
                                    gm_l6 = t47;
                                }
                            }
lb24:;
                        }
                    }
                    {
                        uint8_t t49 = gm_l4;
                        uint8_t t50 = (uint8_t)!t49;
                        if (!t50) goto ie48;
                        {
                            void * t52 = (void *)((uint8_t *)&((struct gm_fr_available_7okjiog *)((&F.h)->up))->l2);
                            gm_tftb1odc739t5 t53 = gm_f_push_d1r4h19(t52, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l3)));
                            gm_tftb1odc739t5 t54 = t53;
                            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srct4pcukr5kqhq, 15, 27);
                            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
                        }
                        goto ix48;
ie48:;
ix48:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4pcukr5kqhq, 9, 9);
me13:;
                }
lc4:;
                {
                    uint64_t t57 = gm_l2;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l2 = t60;
                }
            }
lb4:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_available_7okjiog(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_available_7okjiog F;
    void * gm_l0; /* cx */
    gm_closure gm_l3; /* add */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 962u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        gm_f_list_084fcd6((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_closure t2 = ((gm_closure){(void *)gm_f_add_4m1441i, (void *)&F.h, cx});
        gm_l3 = t2;
    }
    {
        gm_closure t3 = gm_l3;
        void * t4 = gm_l0;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 176) + 0));
        gm_closure t6 = t3;
        gm_node *ccx7 = t6.cx;
        gm_node cn8; { gm_node *f = gm_find(cx, 587034433); if (f) { cn8 = *f; cn8.next = ccx7; ccx7 = &cn8; } }
        void * t9 = t6.code;
        gm_b0_1 t10;
        if (GM_PTR_TAGGED(t9)) {
            ((void (*)(gm_b16_8))GM_PTR_UNTAG(t9))(t5);
        } else {
            t10 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))t9)(t6.env, ccx7, NULL, t5);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_closure t11 = gm_l3;
        void * t12 = gm_l0;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((((uint8_t *)t12) + 1000) + 0));
        gm_b16_8 t14 = t13;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
        gm_closure t16 = t11;
        gm_node *ccx17 = t16.cx;
        gm_node cn18; { gm_node *f = gm_find(cx, 587034433); if (f) { cn18 = *f; cn18.next = ccx17; ccx17 = &cn18; } }
        void * t19 = t16.code;
        gm_b0_1 t20;
        if (GM_PTR_TAGGED(t19)) {
            ((void (*)(gm_b16_8))GM_PTR_UNTAG(t19))(t15);
        } else {
            t20 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))t19)(t16.env, ccx17, NULL, t15);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t22 = gm_l0;
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((((uint8_t *)t22) + 576) + 0));
            gm_l4 = t23;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l5;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t30 = gm_l5;
                    uint64_t t31 = (uintptr_t)t30;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_tjhr0bdm69ios t35 = GM_LD(gm_tjhr0bdm69ios, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t31 * 384));
                    F.l6 = t35;
                    {
                        gm_closure t37 = gm_l3;
                        gm_b16_8 t38 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 48) + 0));
                        gm_closure t39 = t37;
                        gm_node *ccx40 = t39.cx;
                        gm_node cn41; { gm_node *f = gm_find(cx, 587034433); if (f) { cn41 = *f; cn41.next = ccx40; ccx40 = &cn41; } }
                        void * t42 = t39.code;
                        gm_b0_1 t43;
                        if (GM_PTR_TAGGED(t42)) {
                            ((void (*)(gm_b16_8))GM_PTR_UNTAG(t42))(t38);
                        } else {
                            t43 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))t42)(t39.env, ccx40, NULL, t38);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll28;
                        }
                    }
                    {
                        gm_closure t44 = gm_l3;
                        gm_b16_8 t45 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 304) + 0));
                        gm_b16_8 t46 = t45;
                        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                        gm_closure t48 = t44;
                        gm_node *ccx49 = t48.cx;
                        gm_node cn50; { gm_node *f = gm_find(cx, 587034433); if (f) { cn50 = *f; cn50.next = ccx49; ccx49 = &cn50; } }
                        void * t51 = t48.code;
                        gm_b0_1 t52;
                        if (GM_PTR_TAGGED(t51)) {
                            ((void (*)(gm_b16_8))GM_PTR_UNTAG(t51))(t47);
                        } else {
                            t52 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))t51)(t48.env, ccx49, NULL, t47);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll28;
                        }
                    }
                    goto me33;
mn34:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4pcukr5kqhq, 20, 5);
me33:;
                }
lc24:;
                {
                    uint64_t t54 = gm_l5;
                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                    uint64_t t57 = r55;
                    gm_l5 = t57;
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
        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_map_b2f0sd4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_effect_map_b2f0sd4 F;
    gm_b16_8 gm_l0; /* params */
    gm_b16_8 gm_l1; /* args */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint32_t gm_l6; /* p */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_oalq03d((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
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
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint32_t t14 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 4));
                    gm_tn68el8d14vku t15;
                    GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t10);
                    GM_ST(uint32_t, ((uint8_t *)&t15 + 8), t14);
                    gm_tn68el8d14vku t16 = t15;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    gm_l5 = t19;
                    uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&t16) + 8));
                    gm_l6 = t20;
                    {
                        uint64_t t23 = gm_l5;
                        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        uint8_t t25 = (t23 < t24);
                        if (!t25) goto ie22;
                        uint64_t t26 = gm_l5;
                        uint64_t t27 = (uintptr_t)t26;
                        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srct4pcukr5kqhq, 132, 41);
                        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t27 * 40) + 0));
                        if (t29 != ((uint8_t)0ULL)) goto ie22;
                        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t27 * 40) + 8));
                        gm_l7 = t30;
                        {
                            void * t32 = (void *)((uint8_t *)&F.l2);
                            uint32_t t33 = gm_l6;
                            gm_t88rcucg1b7uk t34 = gm_l7;
                            gm_tplpbqf3duvn3 t35;
                            GM_ST(uint32_t, ((uint8_t *)&t35 + 0), t33);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t35 + 4), t34);
                            gm_tftb1odc739t5 t36 = gm_f_push_h69n5fi(t32, GM_CONV(gm_cs7e9ppsd384bj, t35));
                            gm_tftb1odc739t5 t37 = t36;
                            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_srct4pcukr5kqhq, 132, 51);
                            gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4pcukr5kqhq, 131, 5);
me17:;
                }
lc4:;
                {
                    uint64_t t40 = gm_l4;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l4 = t43;
                }
            }
lb4:;
        }
    }
    {
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        uint8_t t45 = 0;
        F.ret = t44;
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
inline __attribute__((aligned(4))) void * gm_f_perform_bound_cc96od0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2, gm_t88rcucg1b7uk a3, const void * a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_perform_bound_cc96od0 F;
    void * gm_l0; /* cx */
    gm_tiilmfd16v3fc gm_l1; /* eref */
    uint32_t gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* ret */
    gm_t6j6smj7qoctu gm_l4; /* bound */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* call */
    GM_STACK_CHECK(&F, 562u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 192);
        gm_tj44oivvogvtl t4;
        memset((uint8_t *)&t4 + 36, 0, 4);
        memcpy((uint8_t *)&t4 + 0, ((uint8_t *)a1), 24);
        gm_tl874t2atnlh3 t5 = gm_l5;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t4 + 24, t5);
        gm_tftb1odc739t5 t6 = gm_f_push_3onuaru(t3, &t4);
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srct4pcukr5kqhq, 122, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        uint32_t t10 = gm_l2;
        uint64_t t11 = (uint64_t)t10;
        uint64_t t12 = (uintptr_t)t11;
        uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)a1) + 0));
        uint64_t t14 = (uint64_t)t13;
        uint64_t t15 = (uintptr_t)t14;
        void * t16 = gm_l0;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
        uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 1048) + 0) + 8);
        if (t15 >= (uintptr_t)(t18)) gm_trap_index(t15, t18, gm_srct4pcukr5kqhq, 123, 8);
        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1048) + 0)) + t15 * 56) + 40) + 8);
        if (t12 >= (uintptr_t)(t19)) gm_trap_index(t12, t19, gm_srct4pcukr5kqhq, 123, 8);
        uint8_t t20 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1048) + 0)) + t15 * 56) + 40)) + t12 * 48) + 16) + 0) == ((uint8_t)0ULL)));
        if (!t20) goto ie9;
        {
            void * t22 = gm_l0;
            void * t23 = (void *)(((uint8_t *)t22) + 536);
            gm_tl874t2atnlh3 t24 = gm_l5;
            gm_tftb1odc739t5 t25 = gm_f_push_0mk7udh(t23, GM_CONV(gm_cseahvg8v7p0vs, t24));
            gm_tftb1odc739t5 t26 = t25;
            if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srct4pcukr5kqhq, 123, 73);
            gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_t88rcucg1b7uk t28 = gm_l3;
        gm_to76jli3bqddq t29;
        gm_t419mo45g7j0a t30;
        memset((uint8_t *)&t30 + 28, 0, 4);
        memcpy((uint8_t *)&t30 + 0, ((uint8_t *)a1), 24);
        uint32_t t31 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t30 + 24, t31);
        gm_tsth9qu1dv0g3 t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t32 + 8), t30);
        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t29 + 0, t32);
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)a4) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t29 + 40, t33);
        gm_teti411eujvf4 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t34 + 8), t29);
        gm_tl874t2atnlh3 t35 = gm_l5;
        void * t36 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t28, &t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t36;
    }
    {
        void * t37 = gm_l0;
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)a4) + 16));
        void * t39 = gm_l6;
        void * t40 = gm_f_finish_call_kjknkfg((void *)NULL, cx, NULL, t37, t38, t39, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t41 = 0;
        F.ret = t40;
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
__attribute__((aligned(4))) void * gm_f_perform_op_1nih4r3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_perform_op_1nih4r3 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* eff */
    uint32_t gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t6j6smj7qoctu gm_l7; /* bound */
    gm_tiilmfd16v3fc gm_l8; /* eref */
    gm_b16_8 gm_l9; /* avail */
    gm_tqu3m2l3n8mnc gm_l10; /* only */
    uint64_t gm_l13; /* positional */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_tkn42u7na3t81 gm_l19; /* expect */
    gm_tiilmfd16v3fc gm_l20; /* one */
    void * gm_l21; /* v */
    uint8_t gm_l22; /* complete */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint32_t gm_l26; /* p */
    gm_t88rcucg1b7uk gm_l27; /* t */
    gm_tiilmfd16v3fc gm_l28; /* eref */
    gm_tiilmfd16v3fc gm_l29; /* one */
    gm_tiilmfd16v3fc gm_l30; /* one */
    gm_b16_8 gm_l31; /* what */
    gm_b16_8 gm_l32; /* m */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* i */
    void * gm_l37; /* v */
    GM_STACK_CHECK(&F, 3593u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 1048) + 0) + 8);
        if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_srct4pcukr5kqhq, 31, 13);
        gm_tammvh1iespi3 t8 = GM_LD(gm_tammvh1iespi3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 1048) + 0)) + t4 * 56));
        F.l5 = t8;
    }
    {
        uint32_t t9 = gm_l2;
        uint64_t t10 = (uint64_t)t9;
        uint64_t t11 = (uintptr_t)t10;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 40) + 8);
        if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_srct4pcukr5kqhq, 32, 10);
        gm_tv0k2vg3054a6 t13 = GM_LD(gm_tv0k2vg3054a6, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 40)) + t11 * 48));
        F.l6 = t13;
    }
    {
        uint64_t t15 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 24) + 8);
        uint8_t t16 = ((t15 == ((uint64_t)0ULL)));
        if (!t16) goto ie14;
        {
            void * t18 = gm_l0;
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 24));
            gm_b16_8 t20 = t19;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 16));
            gm_b16_8 t23 = gm_l3;
            gm_tl874t2atnlh3 t24 = gm_l4;
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sk6ueggk5ji6v));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(9));
            gm_ttu09lij36f8c t26;
            gm_f_bind_args_dnih8ih((void *)&t26, (void *)NULL, cx, NULL, t18, t21, t22, t23, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttu09lij36f8c t27 = t26;
            gm_t6j6smj7qoctu t28;
            if (((((uint8_t *)&t27))[32] != 0)) {
                gm_t6j6smj7qoctu t29 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t27) + 0));
                t28 = t29;
            } else {
                void * t30 = gm_l0;
                void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
                gm_tl874t2atnlh3 t32 = gm_l4;
                void * t33 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t31, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t34 = 0;
                F.ret = t33;
                goto gm_fret;
            }
            gm_l7 = t28;
        }
        {
            memset((uint8_t *)&gm_l8 + 4, 0, 4);
            uint32_t t35 = gm_l1;
            GM_ST(uint32_t, (uint8_t *)&gm_l8 + 0, t35);
            gm_b16_8 t36 = gm_f_empty_slice_fh7svmj();
            gm_b16_8 t37 = t36;
            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
            GM_ST(gm_b16_8, (uint8_t *)&gm_l8 + 8, t38);
        }
        {
            void * t39 = gm_l0;
            uint32_t t40 = gm_l2;
            gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 40));
            gm_tl874t2atnlh3 t42 = gm_l4;
            void * t43 = gm_f_perform_bound_cc96od0((void *)NULL, cx, NULL, t39, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l8)), t40, t41, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l7)), t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t44 = 0;
            F.ret = t43;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t45 = gm_l0;
        uint32_t t46 = gm_l1;
        gm_b16_8 t47 = gm_f_available_7okjiog((void *)NULL, cx, NULL, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t47;
    }
    {
        gm_tqu3m2l3n8mnc t48;
        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        uint8_t t51 = ((t50 == ((uint64_t)1ULL)));
        if (!t51) goto ie49;
        uint64_t t53 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        if (t53 >= (uintptr_t)(t54)) gm_trap_index(t53, t54, gm_srct4pcukr5kqhq, 41, 44);
        gm_tiilmfd16v3fc t55 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t53 * 24));
        gm_tiilmfd16v3fc t56 = t55;
        gm_tqu3m2l3n8mnc t57;
        GM_ST(uint8_t, (uint8_t *)&t57 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t57 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t57 + 28, 0);
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t57), t56);
        ((uint8_t *)&t57)[24] = 1;
        t48 = t57;
        goto ix49;
ie49:;
        gm_tqu3m2l3n8mnc t59;
        memset(&t59, 0, sizeof t59);
        gm_tqu3m2l3n8mnc t60 = t59;
        t48 = t60;
ix49:;
        gm_l10 = t48;
    }
    {
        gm_f_list_oalq03d((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l13 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t62 = gm_l3;
            gm_l14 = t62;
        }
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t64 = gm_l15;
                uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t66 = (t64 < t65);
                if (!t66) goto lb63;
                {
                    uint64_t t69 = gm_l15;
                    uint64_t t70 = (uintptr_t)t69;
                    uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    gm_t7te2tdpebbt4 t74 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t70 * 144));
                    F.l16 = t74;
                    {
                        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)&F.l16) + 0) + 0));
                        if (t77 != ((uint8_t)0ULL)) goto ie76;
                        gm_tc3oht4ksth9d t78 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l16) + 0) + 8));
                        F.l17 = t78;
                        {
                            uint64_t t80 = gm_l13;
                            gm_l18 = t80;
                        }
                        {
                            uint64_t t81 = gm_l13;
                            uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                            if (o83) gm_trap("integer overflow in `+`", gm_srct4pcukr5kqhq, 48, 9);
                            uint64_t t84 = r82;
                            gm_l13 = t84;
                        }
                        {
                            gm_tkn42u7na3t81 t85;
                            memset(&t85, 0, sizeof t85);
                            gm_l19 = t85;
                        }
                        {
                            if (!((((uint8_t *)&gm_l10))[24] != 0)) goto ie86;
                            gm_tiilmfd16v3fc t87 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l10));
                            gm_l20 = t87;
                            {
                                uint64_t t90 = gm_l18;
                                uint64_t t91 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 24) + 8);
                                uint8_t t92 = (t90 < t91);
                                if (!t92) goto ie89;
                                {
                                    void * t94 = gm_l0;
                                    void * t95 = GM_LD(void *, (((uint8_t *)t94) + 0));
                                    uint64_t t96 = gm_l18;
                                    uint64_t t97 = (uintptr_t)t96;
                                    uint64_t t98 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 24) + 8);
                                    if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_srct4pcukr5kqhq, 51, 55);
                                    gm_t88rcucg1b7uk t99 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 24)) + t97 * 56) + 16));
                                    gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 24));
                                    gm_b16_8 t101 = t100;
                                    gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
                                    gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                                    gm_b16_8 t104 = gm_f_effect_map_b2f0sd4((void *)NULL, cx, NULL, t102, t103);
                                    if (gm_unw.kind) {
                                        goto gm_ll67;
                                    }
                                    gm_b16_8 t105 = t104;
                                    gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                                    gm_t88rcucg1b7uk t107 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t95, t99, t106);
                                    if (gm_unw.kind) {
                                        goto gm_ll67;
                                    }
                                    gm_tkn42u7na3t81 t108;
                                    GM_ST(uint8_t, (uint8_t *)&t108 + 5, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t108 + 6, 0);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t108), t107);
                                    ((uint8_t *)&t108)[4] = 1;
                                    gm_l19 = t108;
                                }
                                goto ix89;
ie89:;
ix89:;
                            }
                            goto ix86;
ie86:;
ix86:;
                        }
                        {
                            void * t109 = gm_l0;
                            void * t110 = (void *)((uint8_t *)&F.l17);
                            gm_tkn42u7na3t81 t111 = gm_l19;
                            void * t112 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t109, t110, t111);
                            if (gm_unw.kind) {
                                goto gm_ll67;
                            }
                            gm_l21 = t112;
                        }
                        {
                            uint64_t t114 = gm_l18;
                            uint64_t t115 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 24) + 8);
                            uint8_t t116 = (t114 < t115);
                            if (!t116) goto ie113;
                            {
                                void * t118 = gm_l0;
                                void * t119 = GM_LD(void *, (((uint8_t *)t118) + 0));
                                uint64_t t120 = gm_l18;
                                uint64_t t121 = (uintptr_t)t120;
                                uint64_t t122 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 24) + 8);
                                if (t121 >= (uintptr_t)(t122)) gm_trap_index(t121, t122, gm_srct4pcukr5kqhq, 55, 31);
                                gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 24)) + t121 * 56) + 16));
                                void * t124 = gm_l0;
                                void * t125 = (void *)(((uint8_t *)t124) + 96);
                                void * t126 = gm_l0;
                                void * t127 = GM_LD(void *, (((uint8_t *)t126) + 0));
                                void * t128 = gm_l21;
                                gm_t88rcucg1b7uk t129 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t128) + 0));
                                gm_t88rcucg1b7uk t130 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t125, t127, t129);
                                if (gm_unw.kind) {
                                    goto gm_ll67;
                                }
                                void * t131 = (void *)((uint8_t *)&F.l11);
                                gm_b0_1 t132 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t119, t123, t130, t131);
                                if (gm_unw.kind) {
                                    goto gm_ll67;
                                }
                            }
                            goto ix113;
ie113:;
ix113:;
                        }
                        {
                            void * t133 = (void *)((uint8_t *)&F.l12);
                            void * t134 = gm_l21;
                            gm_tftb1odc739t5 t135 = gm_f_push_d943vgp_2(t133, &(*(gm_t69m56meqefs8 *)((uint8_t *)t134)));
                            gm_tftb1odc739t5 t136 = t135;
                            if (((((uint8_t *)&t136))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t136) + 1, gm_srct4pcukr5kqhq, 57, 9);
                            gm_b0_1 t137 = GM_LD(gm_b0_1, (((uint8_t *)&t136) + 1));
                        }
                        goto ix76;
ie76:;
                        {
                            goto lc63;
                        }
ix76:;
                    }
                    goto me72;
mn73:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4pcukr5kqhq, 45, 5);
me72:;
                }
lc63:;
                {
                    uint64_t t140 = gm_l15;
                    uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
                    uint64_t t143 = r141;
                    gm_l15 = t143;
                }
            }
gm_ll67:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb63; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc63; }
            }
            goto gm_lad;
lb63:;
        }
    }
    {
        gm_l22 = ((uint8_t)1);
    }
    {
        gm_f_list_vmvl81b((void *)&F.l23, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 24));
            gm_l24 = t145;
        }
        {
            gm_l25 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t147 = gm_l25;
                uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                uint8_t t149 = (t147 < t148);
                if (!t149) goto lb146;
                {
                    uint64_t t152 = gm_l25;
                    uint64_t t153 = (uintptr_t)t152;
                    uint64_t t154 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                    uint32_t t155 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t153 * 4));
                    uint32_t t156 = t155;
                    uint32_t t159 = GM_LD(uint32_t, ((uint8_t *)&t156));
                    gm_l26 = t159;
                    {
                        gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                        gm_b16_8 t162 = t161;
                        gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
                        uint32_t t164 = gm_l26;
                        gm_tkn42u7na3t81 t165 = gm_f_bound_to_5c11t0h(t163, t164);
                        gm_tkn42u7na3t81 t166 = t165;
                        gm_t88rcucg1b7uk t167;
                        if (((((uint8_t *)&t166))[4] != 0)) {
                            gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t166) + 0));
                            t167 = t168;
                        } else {
                            {
                                gm_l22 = ((uint8_t)0);
                            }
                            goto lb146;
                        }
                        gm_l27 = t167;
                    }
                    {
                        void * t170 = (void *)((uint8_t *)&F.l23);
                        gm_t88rcucg1b7uk t171 = gm_l27;
                        gm_toa2vhe3l4hfr t172;
                        memset(&t172, 0, sizeof t172);
                        GM_ST(uint8_t, ((uint8_t *)&t172 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t172 + 8), t171);
                        gm_tftb1odc739t5 t173 = gm_f_push_s5vr2a2(t170, &t172);
                        gm_tftb1odc739t5 t174 = t173;
                        if (((((uint8_t *)&t174))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t174) + 1, gm_srct4pcukr5kqhq, 66, 9);
                        gm_b0_1 t175 = GM_LD(gm_b0_1, (((uint8_t *)&t174) + 1));
                    }
                    goto me157;
mn158:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4pcukr5kqhq, 61, 5);
me157:;
                }
lc146:;
                {
                    uint64_t t177 = gm_l25;
                    uint64_t r178; bool o179 = __builtin_add_overflow(t177, ((uint64_t)1ULL), &r178);
                    uint64_t t180 = r178;
                    gm_l25 = t180;
                }
            }
lb146:;
        }
    }
    {
        memset((uint8_t *)&gm_l28 + 4, 0, 4);
        uint32_t t181 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&gm_l28 + 0, t181);
        gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
        gm_b16_8 t183 = t182;
        gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)&t183));
        GM_ST(gm_b16_8, (uint8_t *)&gm_l28 + 8, t184);
    }
    {
        uint8_t t186 = gm_l22;
        if (!t186) goto ie185;
        {
            if (!((((uint8_t *)&gm_l10))[24] != 0)) goto ie188;
            gm_tiilmfd16v3fc t189 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l10));
            gm_l29 = t189;
            {
                void * t192 = (void *)((uint8_t *)&gm_l29);
                void * t193 = (void *)((uint8_t *)&gm_l28);
                int32_t t194 = gm_f_cmp_ref_8ntrqll(t192, t193);
                uint8_t t195 = (!(t194 == ((int32_t)0LL)));
                if (!t195) goto ie191;
                {
                    gm_tiilmfd16v3fc t197 = gm_l29;
                    gm_l28 = t197;
                }
                goto ix191;
ie191:;
ix191:;
            }
            goto ix188;
ie188:;
ix188:;
        }
        goto ix185;
ie185:;
        if (!((((uint8_t *)&gm_l10))[24] != 0)) goto ie198;
        gm_tiilmfd16v3fc t199 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l10));
        gm_l30 = t199;
        {
            gm_tiilmfd16v3fc t201 = gm_l30;
            gm_l28 = t201;
        }
        goto ix198;
ie198:;
        {
            gm_b16_8 t203;
            uint64_t t205 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
            uint8_t t206 = ((t205 == ((uint64_t)0ULL)));
            if (!t206) goto ie204;
            gm_b16_8 t208;
            GM_ST(void *, (uint8_t *)&t208, (void *)(gm_sbiv1tnsmo6hr));
            GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(34));
            gm_b16_8 t209 = t208;
            t203 = t209;
            goto ix204;
ie204:;
            gm_b16_8 t211;
            GM_ST(void *, (uint8_t *)&t211, (void *)(gm_snch62us408pn));
            GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(21));
            gm_b16_8 t212 = t211;
            t203 = t212;
ix204:;
            gm_l31 = t203;
        }
        {
            void * t213 = gm_l0;
            gm_tl874t2atnlh3 t214 = gm_l4;
            gm_t6j6smj7qoctu t215;
            gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t215 + 0, t216);
            gm_b16_8 t217 = gm_l31;
            GM_ST(gm_b16_8, (uint8_t *)&t215 + 16, t217);
            gm_tabb710d9mq2e t218;
            gm_f_format_5qcf83g((void *)&t218, (void *)NULL, cx, NULL, &t215);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t219 = t218;
            if (((((uint8_t *)&t219))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t219) + 8, gm_srct4pcukr5kqhq, 80, 24);
            gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&t219) + 8));
            gm_b16_8 t221 = t220;
            gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&t221));
            gm_b0_1 t223 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t213, t214, t222);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t224 = gm_l0;
            void * t225 = GM_LD(void *, (((uint8_t *)t224) + 0));
            gm_tl874t2atnlh3 t226 = gm_l4;
            void * t227 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t225, t226);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t228 = 0;
            F.ret = t227;
            goto gm_fret;
        }
ix198:;
ix185:;
    }
    {
        gm_b16_8 t229 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 24));
        gm_b16_8 t230 = t229;
        gm_b16_8 t231 = GM_LD(gm_b16_8, ((uint8_t *)&t230));
        gm_b16_8 t232 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
        gm_b16_8 t233 = gm_f_effect_map_b2f0sd4((void *)NULL, cx, NULL, t231, t232);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l32 = t233;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l33, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l35 = ((uint64_t)0ULL);
        }
        {
            uint64_t t235 = GM_LD(uintptr_t, (((uint8_t *)&F.l12) + 0) + 8);
            gm_l34 = t235;
        }
        {
            for (;;) {
                uint64_t t237 = gm_l35;
                uint64_t t238 = gm_l34;
                uint8_t t239 = (t237 < t238);
                if (!t239) goto lb236;
                {
                    uint64_t t242 = gm_l35;
                    uint64_t t243 = t242;
                    uint64_t t246 = GM_LD(uint64_t, ((uint8_t *)&t243));
                    gm_l36 = t246;
                    {
                        uint64_t t248 = gm_l36;
                        uint64_t t249 = (uintptr_t)t248;
                        uint64_t t250 = GM_LD(uintptr_t, (((uint8_t *)&F.l12) + 0) + 8);
                        if (t249 >= (uintptr_t)(t250)) gm_trap_index(t249, t250, gm_srct4pcukr5kqhq, 86, 15);
                        void * t251 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l12) + 0)) + t249 * 88);
                        gm_l37 = t251;
                    }
                    {
                        uint64_t t253 = gm_l36;
                        uint64_t t254 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 24) + 8);
                        uint8_t t255 = (t253 < t254);
                        if (!t255) goto ie252;
                        {
                            void * t257 = (void *)((uint8_t *)&F.l33);
                            void * t258 = gm_l0;
                            void * t259 = gm_l37;
                            void * t260 = gm_l0;
                            void * t261 = GM_LD(void *, (((uint8_t *)t260) + 0));
                            uint64_t t262 = gm_l36;
                            uint64_t t263 = (uintptr_t)t262;
                            uint64_t t264 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 24) + 8);
                            if (t263 >= (uintptr_t)(t264)) gm_trap_index(t263, t264, gm_srct4pcukr5kqhq, 88, 47);
                            gm_t88rcucg1b7uk t265 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 24)) + t263 * 56) + 16));
                            gm_b16_8 t266 = gm_l32;
                            gm_b16_8 t267 = t266;
                            gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&t267));
                            gm_t88rcucg1b7uk t269 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t261, t265, t268);
                            if (gm_unw.kind) {
                                goto gm_ll240;
                            }
                            void * t270 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t258, t259, t269);
                            if (gm_unw.kind) {
                                goto gm_ll240;
                            }
                            gm_tftb1odc739t5 t271 = gm_f_push_d943vgp_2(t257, &(*(gm_t69m56meqefs8 *)((uint8_t *)t270)));
                            gm_tftb1odc739t5 t272 = t271;
                            if (((((uint8_t *)&t272))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t272) + 1, gm_srct4pcukr5kqhq, 88, 13);
                            gm_b0_1 t273 = GM_LD(gm_b0_1, (((uint8_t *)&t272) + 1));
                        }
                        goto ix252;
ie252:;
                        {
                            void * t275 = gm_l0;
                            void * t276 = gm_l37;
                            gm_tl874t2atnlh3 t277 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t276) + 72));
                            gm_b16_8 t278;
                            GM_ST(void *, (uint8_t *)&t278, (void *)(gm_seiuio4eghuvu));
                            GM_ST(uintptr_t, (uint8_t *)&t278 + 8, (uintptr_t)(18));
                            gm_b0_1 t279 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t275, t277, t278);
                            if (gm_unw.kind) {
                                goto gm_ll240;
                            }
                        }
ix252:;
                    }
                    goto me244;
mn245:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4pcukr5kqhq, 85, 5);
me244:;
                }
lc236:;
                {
                    uint64_t t281 = gm_l35;
                    uint64_t r282; bool o283 = __builtin_add_overflow(t281, ((uint64_t)1ULL), &r282);
                    uint64_t t284 = r282;
                    gm_l35 = t284;
                }
            }
gm_ll240:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb236; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc236; }
            }
            goto gm_lad;
lb236:;
        }
    }
    {
        uint8_t t286 = 0;
        uint64_t t287 = GM_LD(uintptr_t, (((uint8_t *)&F.l33) + 0) + 8);
        uint64_t t288 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 24) + 8);
        uint8_t t289 = (t287 < t288);
        if (!t289) {
            uint64_t t290 = gm_l13;
            uint64_t t291 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t292 = (!(t290 == t291));
            t286 = t292;
        } else t286 = 1;
        if (!t286) goto ie285;
        {
            void * t294 = gm_l0;
            gm_tl874t2atnlh3 t295 = gm_l4;
            gm_t3877pk7lv748 t296;
            gm_b16_8 t297 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t296 + 0, t297);
            uint64_t t298 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 24) + 8);
            GM_ST(uint64_t, (uint8_t *)&t296 + 16, t298);
            gm_tabb710d9mq2e t299;
            gm_f_format_9nalvct((void *)&t299, (void *)NULL, cx, NULL, &t296);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t300 = t299;
            if (((((uint8_t *)&t300))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t300) + 8, gm_srct4pcukr5kqhq, 94, 24);
            gm_b16_8 t301 = GM_LD(gm_b16_8, (((uint8_t *)&t300) + 8));
            gm_b16_8 t302 = t301;
            gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&t302));
            gm_b0_1 t304 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t294, t295, t303);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix285;
ie285:;
ix285:;
    }
    {
        void * t305 = gm_l0;
        void * t306 = (void *)(((uint8_t *)t305) + 192);
        gm_tj44oivvogvtl t307;
        memset((uint8_t *)&t307 + 36, 0, 4);
        memcpy((uint8_t *)&t307 + 0, ((uint8_t *)&gm_l28), 24);
        gm_tl874t2atnlh3 t308 = gm_l4;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t307 + 24, t308);
        gm_tftb1odc739t5 t309 = gm_f_push_3onuaru(t306, &t307);
        gm_tftb1odc739t5 t310 = t309;
        if (((((uint8_t *)&t310))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t310) + 1, gm_srct4pcukr5kqhq, 96, 5);
        gm_b0_1 t311 = GM_LD(gm_b0_1, (((uint8_t *)&t310) + 1));
    }
    {
        uint8_t t313 = ((GM_LD(uint8_t, (((uint8_t *)&F.l6) + 16) + 0) == ((uint8_t)0ULL)));
        if (!t313) goto ie312;
        {
            void * t315 = gm_l0;
            void * t316 = (void *)(((uint8_t *)t315) + 536);
            gm_tl874t2atnlh3 t317 = gm_l4;
            gm_tftb1odc739t5 t318 = gm_f_push_0mk7udh(t316, GM_CONV(gm_cseahvg8v7p0vs, t317));
            gm_tftb1odc739t5 t319 = t318;
            if (((((uint8_t *)&t319))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t319) + 1, gm_srct4pcukr5kqhq, 97, 24);
            gm_b0_1 t320 = GM_LD(gm_b0_1, (((uint8_t *)&t319) + 1));
        }
        goto ix312;
ie312:;
ix312:;
    }
    {
        void * t321 = gm_l0;
        void * t322 = GM_LD(void *, (((uint8_t *)t321) + 0));
        gm_t88rcucg1b7uk t323 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 40));
        gm_b16_8 t324 = gm_l32;
        gm_b16_8 t325 = t324;
        gm_b16_8 t326 = GM_LD(gm_b16_8, ((uint8_t *)&t325));
        gm_t88rcucg1b7uk t327 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t322, t323, t326);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to76jli3bqddq t328;
        gm_t419mo45g7j0a t329;
        memset((uint8_t *)&t329 + 28, 0, 4);
        memcpy((uint8_t *)&t329 + 0, ((uint8_t *)&gm_l28), 24);
        uint32_t t330 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t329 + 24, t330);
        gm_tsth9qu1dv0g3 t331;
        GM_ST(uint8_t, (uint8_t *)&t331 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t331 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t331 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t331 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t331 + 8), t329);
        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t328 + 0, t331);
        gm_b16_8 t332 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t328 + 40, t332);
        gm_teti411eujvf4 t333;
        GM_ST(uint8_t, (uint8_t *)&t333 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t333 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t333 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t333 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t333 + 8), t328);
        gm_tl874t2atnlh3 t334 = gm_l4;
        void * t335 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t327, &t333, t334);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t336 = 0;
        F.ret = t335;
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
__attribute__((aligned(4))) void * gm_f_perform_op_as_9ncunsc(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_perform_op_as_9ncunsc F;
    void * gm_l0; /* cx */
    gm_tiilmfd16v3fc gm_l1; /* eref */
    uint32_t gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l7; /* m */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_t6j6smj7qoctu gm_l13; /* bound */
    GM_STACK_CHECK(&F, 1313u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)a1) + 0));
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 1048) + 0) + 8);
        if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_srct4pcukr5kqhq, 103, 13);
        gm_tammvh1iespi3 t8 = GM_LD(gm_tammvh1iespi3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 1048) + 0)) + t4 * 56));
        F.l5 = t8;
    }
    {
        uint32_t t9 = gm_l2;
        uint64_t t10 = (uint64_t)t9;
        uint64_t t11 = (uintptr_t)t10;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 40) + 8);
        if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_srct4pcukr5kqhq, 104, 10);
        gm_tv0k2vg3054a6 t13 = GM_LD(gm_tv0k2vg3054a6, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 40)) + t11 * 48));
        F.l6 = t13;
    }
    {
        uint64_t t15 = GM_LD(uintptr_t, (((uint8_t *)a1) + 8) + 8);
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 24) + 8);
        uint8_t t17 = (!(t15 == t16));
        if (!t17) goto ie14;
        {
            void * t19 = gm_l0;
            gm_tl874t2atnlh3 t20 = gm_l4;
            gm_t3877pk7lv748 t21;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t21 + 0, t22);
            uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 24) + 8);
            GM_ST(uint64_t, (uint8_t *)&t21 + 16, t23);
            gm_tabb710d9mq2e t24;
            gm_f_format_ql3u2no((void *)&t24, (void *)NULL, cx, NULL, &t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t25 = t24;
            if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_srct4pcukr5kqhq, 106, 24);
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
            gm_b16_8 t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            gm_b0_1 t29 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t19, t20, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t30 = gm_l0;
            void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
            gm_tl874t2atnlh3 t32 = gm_l4;
            void * t33 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t34 = 0;
            F.ret = t33;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 24));
        gm_b16_8 t36 = t35;
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
        gm_b16_8 t39 = gm_f_effect_map_b2f0sd4((void *)NULL, cx, NULL, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t39;
    }
    {
        gm_f_list_af74s0q((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 24));
            gm_l9 = t41;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t43 = gm_l10;
                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t45 = (t43 < t44);
                if (!t45) goto lb42;
                {
                    uint64_t t48 = gm_l10;
                    uint64_t t49 = (uintptr_t)t48;
                    uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_t5v0c8ia6a5mo t53 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t49 * 56));
                    F.l11 = t53;
                    {
                        gm_t5v0c8ia6a5mo t55 = F.l11;
                        F.l12 = t55;
                    }
                    {
                        void * t56 = gm_l0;
                        void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
                        gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                        gm_b16_8 t59 = gm_l7;
                        gm_b16_8 t60 = t59;
                        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                        gm_t88rcucg1b7uk t62 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t57, t58, t61);
                        if (gm_unw.kind) {
                            goto gm_ll46;
                        }
                        GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 16), t62);
                    }
                    {
                        void * t63 = (void *)((uint8_t *)&F.l8);
                        gm_tftb1odc739t5 t64 = gm_f_push_o8u01sh(t63, &(*(gm_t5v0c8ia6a5mo *)((uint8_t *)&F.l12)));
                        gm_tftb1odc739t5 t65 = t64;
                        if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_srct4pcukr5kqhq, 114, 9);
                        gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
                    }
                    goto me51;
mn52:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4pcukr5kqhq, 111, 5);
me51:;
                }
lc42:;
                {
                    uint64_t t68 = gm_l10;
                    uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                    uint64_t t71 = r69;
                    gm_l10 = t71;
                }
            }
gm_ll46:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb42; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc42; }
            }
            goto gm_lad;
lb42:;
        }
    }
    {
        void * t72 = gm_l0;
        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t74 = t73;
        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
        uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 16));
        gm_b16_8 t77 = gm_l3;
        gm_tl874t2atnlh3 t78 = gm_l4;
        gm_b16_8 t79;
        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sk6ueggk5ji6v));
        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(9));
        gm_ttu09lij36f8c t80;
        gm_f_bind_args_dnih8ih((void *)&t80, (void *)NULL, cx, NULL, t72, t75, t76, t77, t78, t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t81 = t80;
        gm_t6j6smj7qoctu t82;
        if (((((uint8_t *)&t81))[32] != 0)) {
            gm_t6j6smj7qoctu t83 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t81) + 0));
            t82 = t83;
        } else {
            void * t84 = gm_l0;
            void * t85 = GM_LD(void *, (((uint8_t *)t84) + 0));
            gm_tl874t2atnlh3 t86 = gm_l4;
            void * t87 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t88 = 0;
            F.ret = t87;
            goto gm_fret;
        }
        gm_l13 = t82;
    }
    {
        void * t89 = gm_l0;
        gm_tiilmfd16v3fc t90 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)a1));
        uint32_t t91 = gm_l2;
        void * t92 = gm_l0;
        void * t93 = GM_LD(void *, (((uint8_t *)t92) + 0));
        gm_t88rcucg1b7uk t94 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 40));
        gm_b16_8 t95 = gm_l7;
        gm_b16_8 t96 = t95;
        gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
        gm_t88rcucg1b7uk t98 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t93, t94, t97);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t99 = gm_l4;
        void * t100 = gm_f_perform_bound_cc96od0((void *)NULL, cx, NULL, t89, &t90, t91, t98, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l13)), t99);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t101 = 0;
        F.ret = t100;
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
